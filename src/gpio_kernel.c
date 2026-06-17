// Prefix every pr_*() line with the module name, e.g. "gpio_kernel: ...".
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>  // copy_from_user
#include <linux/io.h>       // ioremap
#include <linux/cdev.h>     // cdev_init add 
#include <linux/device.h>   //  class_create, device_create
#include <asm/barrier.h>    //mb
#include <linux/delay.h> // msleep 
#include <linux/atomic.h> // atomic_t
#include <linux/spinlock.h>
#include <linux/slab.h> // kmem_cache operations
#include <linux/interrupt.h>
#include <linux/list.h>  // For struct list_head
#include <linux/gpio.h>
#include <linux/version.h>

#include "gpio_kernel.h"     //  ioctl macros

/*
 * Timer helpers were renamed in recent kernels. These shims let the driver
 * build both on the Raspberry Pi's (older) kernel and on recent mainline.
 *   del_timer_sync() -> timer_delete_sync()   (6.2)
 *   from_timer()     -> timer_container_of()  (6.16)
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 2, 0)
#define timer_delete_sync(t) del_timer_sync(t)
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(6, 16, 0)
#define timer_container_of(var, t, field) from_timer(var, t, field)
#endif

// GLOBAL VARS & FORWARD DECLARATIONS //

static int set_pin_direction(unsigned int pin_number, unsigned int direction);
static int set_pin_state(unsigned int pin_number, unsigned int state);
static int start_flicker(unsigned int pin_number, unsigned int interval_ms);
static int stop_flicker(unsigned int pin_number);
static void gpio_timer_callback(struct timer_list* timer);
static int gpio_open(struct inode* inode, struct file* file);
static int gpio_release(struct inode* inode, struct file* file);
static size_t gpio_buffer_available_space(void);
static size_t gpio_buffer_available_data(void);
static ssize_t gpio_write(struct file* file, const char __user* buf, size_t count, loff_t* offset);
static ssize_t gpio_read(struct file* file, char __user* buf, size_t count, loff_t* offset);
static void gpio_irq_init(void);
static int gpio_kgdb_test(int x, int y);

static irqreturn_t gpio_irq_handler(int irq, void* data);
// Global variables for Read/Write
static char buffer[BUFFER_SIZE];
static size_t read_pos = 0;
static size_t write_pos = 0;
static DEFINE_SPINLOCK(rw_spinlock);
// Global variables for GPIO control
static dev_t dev = 0;
static struct cdev gpio_cdev;
static struct class* gpio_class;
static struct device* gpio_device;
static atomic_t gpio_device_opened = ATOMIC_INIT(0);
static void __iomem* gpio_base_virtual = NULL;
static void __iomem* irq_enable1 = NULL;
static int major_num = 0;
static struct kmem_cache* gpio_flicker_cache = NULL;


LIST_HEAD(gpio_flicker_list);
struct gpio_flicker
{
    struct list_head list;
    struct timer_list timer;
    unsigned int pin_number;
    unsigned int interval_ms;
    unsigned int current_state;
};

// Active flicker timers, indexed by pin. Access from ioctl context is
// serialized by the single-opener policy enforced in gpio_open(); the timer
// callback only touches its own node, and stop_flicker()'s timer_delete_sync()
// guarantees the callback isn't running before the node is freed.
static struct gpio_flicker* flickers[MAX_GPIO_PINS] = { NULL };

static const struct file_operations gpio_fops =
{
    .owner = THIS_MODULE,
    .unlocked_ioctl = gpio_ioctl,
    .open = gpio_open,
    .release = gpio_release,
    .read = gpio_read,
    .write = gpio_write
};

////////////////////////

long gpio_ioctl(struct file* file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    struct gpio_pin pin_data;

    ret = copy_from_user(&pin_data, (struct gpio_pin __user*)arg, sizeof(pin_data));
    if (SUCCESS != ret)
    {
        return -EFAULT;
    }

    // pin_number comes from user space and is used to index flickers[];
    // validate it for every command that touches a pin (all but the KGDB test).
    if (GPIO_KGDB_TEST != cmd &&
        (pin_data.pin_number < 0 || pin_data.pin_number >= MAX_GPIO_PINS))
    {
        pr_warn("invalid pin number %d (valid 0-%d)\n",
            pin_data.pin_number, MAX_GPIO_PINS - 1);
        return -EINVAL;
    }

    switch (cmd)
    {
    case GPIO_SET_PIN_DIR:

        if (NULL != flickers[pin_data.pin_number])
        {
            pr_info("pin %d is flickering, turning flicker off first\n", pin_data.pin_number);
            if (SUCCESS != stop_flicker(pin_data.pin_number))
            {
                return -EINVAL; // Invalid argument error code   
            }
        }
        ret = set_pin_direction(pin_data.pin_number, pin_data.direction);

        if (SUCCESS != ret)
        {

            return ret;
        }

        pr_info("Setting GPIO pin %d to direction %d\n", pin_data.pin_number, pin_data.direction);

        break;

    case GPIO_SET_PIN_STATE:
        // Set the GPIO pin state (high/low)
        if (NULL != flickers[pin_data.pin_number])
        {
            pr_info("pin %d is flickering, turning flicker off first\n", pin_data.pin_number);
            if (SUCCESS != stop_flicker(pin_data.pin_number))
            {
                return -EINVAL; // Invalid argument error code   
            }
        }
        ret = set_pin_state(pin_data.pin_number, pin_data.state);
        if (SUCCESS != ret)
        {
            return ret;
        }
        pr_info("Setting GPIO pin %d to state %d\n", pin_data.pin_number, pin_data.state);

        break;

    case GPIO_START_FLICKER:
        // Start flickering the GPIO pin
        ret = start_flicker(pin_data.pin_number, pin_data.flicker_speed);
        if (SUCCESS != ret)
        {
            return ret;
        }
        pr_info("Starting flicker on GPIO pin %d with speed %d ms\n", pin_data.pin_number, pin_data.flicker_speed);

        break;

    case GPIO_STOP_FLICKER:
        ret = stop_flicker(pin_data.pin_number);
        if (SUCCESS != ret)
        {
            return ret;
        }

        break;

    case GPIO_KGDB_TEST:
        ret = gpio_kgdb_test(pin_data.x, pin_data.y);
        pr_info("Result: X + Y = %d\n", ret);
        break;

    default:
        return -ENOTTY;  // Invalid command
    }

    return 0;
}

// (input or output)
static int set_pin_direction(unsigned int pin_number, unsigned int direction)
{
    unsigned int reg_offset = 0; // Offset For The Relevant GPFSEL  
    unsigned int bit_shift = 0;   // Relevant Bits For Specific Pin 
    volatile unsigned int* reg = NULL; // Pointer To the Relevant GPFSEL
    unsigned int val = 0; // GPFSEL Value
    unsigned int read_back = 0; // For Error Handling

    if (pin_number >= MAX_GPIO_PINS)
    {
        pr_warn("invalid pin number %u (valid 0-%d)\n", pin_number, MAX_GPIO_PINS - 1);
        return -EINVAL;
    }

    if (direction > 1)
    {
        pr_warn("invalid direction %u (valid 0 or 1)\n", direction);
        return -EINVAL;
    }

    reg_offset = (pin_number / 10) * 4;
    bit_shift = (pin_number % 10) * 3;
    reg = (volatile unsigned int*)((volatile char*)gpio_base_virtual + reg_offset);

    // Read-modify-write the 3 function-select bits for this pin.
    val = readl(reg);
    val &= ~(0x7 << bit_shift);          // clear the pin's bits
    val |= (direction << bit_shift);     // set input (0) / output (1)
    writel(val, reg);
    mb();                                // order the write before the read-back

    // Verify the write took effect.
    read_back = readl(reg);
    if (read_back != val)
    {
        pr_err("read-back mismatch on pin %u (wrote 0x%08X, got 0x%08X)\n",
            pin_number, val, read_back);
        return -EIO;  // I/O error
    }

    return 0;
}

// gpio pin state (high or low)
static int set_pin_state(unsigned int pin_number, unsigned int state)
{
    unsigned int reg_offset = 0;      // Offset for the relevant GPSET / GPCLR
    volatile unsigned int* reg = NULL;  // Pointer to the relevant register
    unsigned int bit_shift = 0;       // Relevant bits for the specific pin
    unsigned int val = 0;
    if (pin_number >= MAX_GPIO_PINS)
    {
        pr_warn("invalid pin number %u (valid 0-%d)\n", pin_number, MAX_GPIO_PINS - 1);
        return -EINVAL;
    }

    if (state > 1)
    {
        pr_warn("invalid state %u (valid 0 or 1)\n", state);
        return -EINVAL;
    }

    // GPSET sets a pin high, GPCLR sets it low; each is a write-1-to-act register.
    bit_shift = pin_number % 32;
    reg_offset = state == 1 ? GPIO_SET0_OFFSET : GPIO_CLR0_OFFSET;
    reg_offset += (pin_number / 32) * 4;
    reg = (volatile unsigned int*)((volatile char*)gpio_base_virtual + reg_offset);
    val = (1 << bit_shift);
    writel(val, reg);
    mb();

    return 0;
}

static int start_flicker(unsigned int pin_number, unsigned int interval_ms)
{
    // Check if the pin is already flickering
    if (NULL != flickers[pin_number])
    {
        pr_info("Timer already exists for GPIO %u, modifying timer\n", pin_number);
        mod_timer(&flickers[pin_number]->timer, jiffies + msecs_to_jiffies(interval_ms));
        flickers[pin_number]->interval_ms = interval_ms;
    }
    else
    {// Allocate memory for the timer
        flickers[pin_number] = kmem_cache_alloc(gpio_flicker_cache, GFP_KERNEL);
        if (NULL == flickers[pin_number])
        {
            pr_err("Failed to allocate timer for GPIO %u\n", pin_number);
            return -ENOMEM;
        }
        list_add(&flickers[pin_number]->list, &gpio_flicker_list);
        // Initialize the timer to call the callback function
        timer_setup(&flickers[pin_number]->timer, gpio_timer_callback, 0);
        // Set the timer to  after current time + interval_ms
        flickers[pin_number]->timer.expires = jiffies + msecs_to_jiffies(interval_ms);
        add_timer(&flickers[pin_number]->timer);
        flickers[pin_number]->pin_number = pin_number;
        flickers[pin_number]->interval_ms = interval_ms;
        flickers[pin_number]->current_state = 0;
    }

    pr_info("Started blinking on GPIO %u\n", pin_number);
    return 0;
}


static int stop_flicker(unsigned int pin_number)
{
    if (pin_number >= MAX_GPIO_PINS)
    {
        pr_err("Invalid pin number %u\n", pin_number);
        return -EINVAL;
    }
    // Check if there is a flicker running for the specified pin
    if (flickers[pin_number] != NULL)
    {
        //Remove from list
        list_del(&flickers[pin_number]->list);
        // Stop the timer
        timer_delete_sync(&flickers[pin_number]->timer);

        // Free the memory allocated for the flicker
        kmem_cache_free(gpio_flicker_cache, flickers[pin_number]);
        smp_wmb();
        // Set the flicker pointer to NULL to indicate that it's stopped
        flickers[pin_number] = NULL;

        pr_info("Stopped flickering on GPIO %u\n", pin_number);
    }
    else
    {
        pr_info("No flicker found for GPIO %u\n", pin_number);
        return -EINVAL;
    }

    return 0;
}

static void gpio_timer_callback(struct timer_list* timer)
{
    struct gpio_flicker* flicker = timer_container_of(flicker, timer, timer);
    unsigned long flags = 0;
    local_irq_save(flags);
    flicker->current_state ^= 1;
    set_pin_state(flicker->pin_number, flicker->current_state);
    local_irq_restore(flags);
    mod_timer(&flicker->timer, jiffies + msecs_to_jiffies(flicker->interval_ms));
}
// open and release functions

static int gpio_open(struct inode* inode, struct file* file)
{

    if (atomic_cmpxchg(&gpio_device_opened, 0, 1) != 0)
    {
        return -EBUSY;
    }
    pr_info("device opened\n");

    return 0;
}

static int gpio_release(struct inode* inode, struct file* file)
{
    atomic_set(&gpio_device_opened, 0);
    pr_info("GPIO device closed\n");
    return 0;
}
static size_t gpio_buffer_available_space(void)
{
    if (write_pos >= read_pos)
    {
        return BUFFER_SIZE - (write_pos - read_pos) - 1;
    }
    else
    {
        return (read_pos - write_pos) - 1;
    }
}
static size_t gpio_buffer_available_data(void)
{
    if (write_pos >= read_pos)
    {
        return write_pos - read_pos;
    }
    else
    {
        return BUFFER_SIZE - (read_pos - write_pos);
    }
}

static ssize_t gpio_read(struct file* file, char __user* buf, size_t count, loff_t* offset)
{
    size_t data_to_read;
    size_t first_chunk, second_chunk;
    char* local_buffer;
    unsigned long flags;

    // Bounce buffer so we never call copy_to_user() while holding the spinlock.
    // Allocate before taking the lock (kmalloc with GFP_KERNEL may sleep).
    count = min(count, (size_t)BUFFER_SIZE);
    if (count == 0)
        return 0;

    local_buffer = kmalloc(count, GFP_KERNEL);
    if (!local_buffer)
        return -ENOMEM;

    spin_lock_irqsave(&rw_spinlock, flags);

    data_to_read = gpio_buffer_available_data();
    if (data_to_read == 0)
    {
        spin_unlock_irqrestore(&rw_spinlock, flags);
        kfree(local_buffer);
        return 0;  // No data available
    }

    if (count > data_to_read)
    {
        count = data_to_read;
    }

    // Step 1: Handle possible wrap-around
    first_chunk = min(count, BUFFER_SIZE - read_pos);
    memcpy(local_buffer, &buffer[read_pos], first_chunk);

    second_chunk = count - first_chunk;
    if (second_chunk > 0)
        memcpy(local_buffer + first_chunk, buffer, second_chunk);

    read_pos = (read_pos + count) % BUFFER_SIZE;

    spin_unlock_irqrestore(&rw_spinlock, flags);

    // Step 2: Copy to user space
    if (copy_to_user(buf, local_buffer, count))
    {
        kfree(local_buffer);
        return -EFAULT;
    }

    kfree(local_buffer);
    *offset += count;
    return count;
}
static ssize_t gpio_write(struct file* file, const char __user* buf, size_t count, loff_t* offset)
{
    char* local_buffer;
    size_t first_chunk, second_chunk;
    unsigned long flags;

    if (count > BUFFER_SIZE)
        return -EINVAL;
    if (count == 0)
        return 0;

    local_buffer = kmalloc(count, GFP_KERNEL);
    if (!local_buffer)
        return -ENOMEM;

    // Step 1: Copy from user to local buffer
    if (copy_from_user(local_buffer, buf, count))
    {
        kfree(local_buffer);
        return -EFAULT;
    }

    spin_lock_irqsave(&rw_spinlock, flags);

    if (count > gpio_buffer_available_space()) {
        spin_unlock_irqrestore(&rw_spinlock, flags);
        kfree(local_buffer);
        return -ENOSPC;
    }

    // Step 2: Handle possible wrap-around
    first_chunk = min(count, BUFFER_SIZE - write_pos);
    memcpy(&buffer[write_pos], local_buffer, first_chunk);

    second_chunk = count - first_chunk;
    if (second_chunk > 0)
        memcpy(buffer, local_buffer + first_chunk, second_chunk);

    write_pos = (write_pos + count) % BUFFER_SIZE;

    spin_unlock_irqrestore(&rw_spinlock, flags);

    kfree(local_buffer);
    *offset += count;
    return count;
}

static irqreturn_t gpio_irq_handler(int irq, void* data)
{
    volatile unsigned int* eds_reg = (volatile unsigned int*)((volatile char*)gpio_base_virtual + GPIO_GPEDS0);
    writel((1 << GPIO_IRQ_PIN), eds_reg);  // ack: write-1-to-clear the event latch

    pr_info("rising edge detected on GPIO %d\n", GPIO_IRQ_PIN);

    return IRQ_HANDLED;
}

static void gpio_irq_init(void)
{
    volatile unsigned int* eds_reg = (volatile unsigned int*)((volatile char*)gpio_base_virtual + GPIO_GPEDS0);
    volatile unsigned int* pre_reg = (volatile unsigned int*)((volatile char*)gpio_base_virtual + GPIO_GPREN0);
    unsigned int irq_enable_val = 0;
    unsigned int pre_val = 0;

    // IRQ_ENABLE1 covers IRQs 32-63, so the GPIO IRQ bit is (GPIO_IRQ_NUM - 32).
    irq_enable1 = ioremap(IRQ_ENABLE1_ADDR, 4);
    if (!irq_enable1) {
        pr_err("failed to map IRQ_ENABLE1 register\n");
        return;
    }
    irq_enable_val = readl(irq_enable1);
    irq_enable_val |= (1 << (GPIO_IRQ_NUM - 32));
    writel(irq_enable_val, irq_enable1);

    // Clear any stale event, then enable rising-edge detection on the pin.
    writel((1 << GPIO_IRQ_PIN), eds_reg);
    pre_val = readl(pre_reg);
    pre_val |= (1 << GPIO_IRQ_PIN);
    writel(pre_val, pre_reg);
}

static int gpio_kgdb_test(int x, int y)
{
    pr_info("%s called with x=%d, y=%d\n", __func__, x, y);
    return x + y;
}


// init and exit funcs

static int __init gpio_init(void)
{
    int ret;


    pr_info("Initializing driver...\n");

    // Create slab cache for flicker timers
    gpio_flicker_cache = kmem_cache_create("gpio_timer_cache", sizeof(struct gpio_flicker), 0,
        SLAB_HWCACHE_ALIGN, NULL);
    if (!gpio_flicker_cache) {
        pr_err("Failed to create timer slab cache\n");
        return -ENOMEM;
    }
    pr_info("Timer slab cache created successfully\n");

    // Allocate character device region
    ret = alloc_chrdev_region(&dev, 0, 1, DRIVER_NAME);
    if (ret < 0) {
        pr_err("Failed to allocate character device region, ret = %d\n", ret);
        kmem_cache_destroy(gpio_flicker_cache);  // Clean up slab cache
        return ret;
    }
    major_num = MAJOR(dev);
    pr_info("Character device region allocated, major number = %d\n", major_num);

    // Map GPIO registers to virtual memory
    gpio_base_virtual = ioremap(GPIO_BASE_ADDR, GPIO_REGS_SIZE);
    if (!gpio_base_virtual) {
        pr_err("Failed to map GPIO registers to virtual memory\n");
        unregister_chrdev_region(dev, 1);  // Release character device region
        kmem_cache_destroy(gpio_flicker_cache);  // Clean up slab cache
        return -ENOMEM;
    }
    pr_info("GPIO registers mapped to virtual memory\n");

    // Initialize and add cdev
    cdev_init(&gpio_cdev, &gpio_fops);
    gpio_cdev.owner = THIS_MODULE;
    ret = cdev_add(&gpio_cdev, dev, 1);
    if (ret < 0) {
        pr_err("Failed to add cdev, ret = %d\n", ret);
        iounmap(gpio_base_virtual);  // Unmap GPIO registers
        unregister_chrdev_region(dev, 1);  // Release character device region
        kmem_cache_destroy(gpio_flicker_cache);  // Clean up slab cache
        return ret;
    }
    pr_info("cdev added successfully\n");

    // Create device class
    gpio_class = class_create(DRIVER_NAME);
    if (IS_ERR(gpio_class)) {
        ret = PTR_ERR(gpio_class);
        pr_err("Failed to create class, ret = %d\n", ret);
        cdev_del(&gpio_cdev);  // Remove cdev
        iounmap(gpio_base_virtual);  // Unmap GPIO registers
        unregister_chrdev_region(dev, 1);  // Release character device region
        kmem_cache_destroy(gpio_flicker_cache);  // Clean up slab cache
        return ret;
    }
    pr_info("Device class created successfully\n");

    // Create device node in /dev/
    gpio_device = device_create(gpio_class, NULL, dev, NULL, DRIVER_NAME);
    if (IS_ERR(gpio_device)) {
        ret = PTR_ERR(gpio_device);
        pr_err("Failed to create device, ret = %d\n", ret);
        class_destroy(gpio_class);  // Destroy device class
        cdev_del(&gpio_cdev);  // Remove cdev
        iounmap(gpio_base_virtual);  // Unmap GPIO registers
        unregister_chrdev_region(dev, 1);  // Release character device region
        kmem_cache_destroy(gpio_flicker_cache);  // Clean up slab cache
        return ret;
    }
    pr_info("Device node created successfully\n");

    // Set GPIO GPIO_IRQ_PIN direction
    if (SUCCESS != set_pin_direction(GPIO_IRQ_PIN, GPIO_INPUT)) {
        pr_err("Failed to set GPIO_IRQ_PIN direction\n");
        device_destroy(gpio_class, dev);  // Destroy device node
        class_destroy(gpio_class);  // Destroy device class
        cdev_del(&gpio_cdev);  // Remove cdev
        iounmap(gpio_base_virtual);  // Unmap GPIO registers
        unregister_chrdev_region(dev, 1);  // Release character device region
        kmem_cache_destroy(gpio_flicker_cache);  // Clean up slab cache
        return -EINVAL;
    }
    pr_info("GPIO_IRQ_PIN direction set to input\n");
    gpio_irq_init();


    // Request IRQ
    ret = request_irq(GPIO_IRQ_NUM, gpio_irq_handler,
        IRQF_TRIGGER_RISING,
        "gpio_irq", NULL);
    if (ret) {
        pr_err("Failed to request IRQ, ret = %d\n", ret);
        device_destroy(gpio_class, dev);  // Destroy device node
        class_destroy(gpio_class);  // Destroy device class
        cdev_del(&gpio_cdev);  // Remove cdev
        iounmap(gpio_base_virtual);  // Unmap GPIO registers
        unregister_chrdev_region(dev, 1);  // Release character device region
        kmem_cache_destroy(gpio_flicker_cache);  // Clean up slab cache
        return ret;
    }
    pr_info("IRQ requested successfully\n");

    pr_info("Driver initialized successfully\n");
    return 0;
}

static void __exit gpio_exit(void)
{
    struct gpio_flicker* flicker;
    struct gpio_flicker* tmp;
    if (gpio_flicker_cache)
    {
        // _safe: we free each node as we go, so we must stash the next pointer.
        list_for_each_entry_safe(flicker, tmp, &gpio_flicker_list, list)
        {
            list_del(&flicker->list);
            timer_delete_sync(&flicker->timer);
            flickers[flicker->pin_number] = NULL;
            kmem_cache_free(gpio_flicker_cache, flicker);
        }
    }
    free_irq(GPIO_IRQ_NUM, NULL);


    device_destroy(gpio_class, dev);
    class_destroy(gpio_class);
    cdev_del(&gpio_cdev);
    unregister_chrdev_region(dev, 1);  // 1 = NUMBER OF MINOR NUMS e.g devices that uses the device driver
    if (NULL != gpio_base_virtual)
    {
        iounmap(gpio_base_virtual);
    }
    if (irq_enable1)
    {
        iounmap(irq_enable1);
    }
    kmem_cache_destroy(gpio_flicker_cache);
    pr_info("GPIO driver exited\n");
}

module_init(gpio_init);
module_exit(gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raz");
MODULE_DESCRIPTION("gpio driver for raspberry pi");
