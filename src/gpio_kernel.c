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
#include <linux/kthread.h>   // kthread_run, kthread_stop,  task_struct
#include <linux/delay.h>     // msleep()
#include "gpio_kernel.h"     //  ioctl macros





// GLOBAL VARS & FORWARD DECLARATIONS //

int SetPinDirection(unsigned int pin_number, unsigned int direction);
int SetPinState(unsigned int pin_number, unsigned int state);
int StartFlicker(unsigned int pin, unsigned int speed_ms);
static int FlickerThread(void* data);
long gpio_ioctl(struct file* file, unsigned int cmd, unsigned long arg);
static int GPIOOpen(struct inode* inode, struct file* file);
static int GPIORelease(struct inode* inode, struct file* file);


static dev_t dev = 0;
static struct cdev gpio_cdev;
static struct class* gpio_class;
static struct device* gpio_device;
static atomic_t gpio_device_opened = ATOMIC_INIT(0);
static void __iomem* gpio_base_virtual = NULL;
static int major_num = 0;

struct flicker_info {
    int is_enabled;
    int speed_ms;
};
static struct flicker_info flickers[MAX_GPIO_PINS];

static struct task_struct* flicker_threads[MAX_GPIO_PINS] = { NULL };


static const struct file_operations gpio_fops =
{
    .owner = THIS_MODULE,
    .unlocked_ioctl = gpio_ioctl,
    .open = GPIOOpen,
    .release = GPIORelease,
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

    switch (cmd) {
    case GPIO_SET_PIN_DIR:

        printk(KERN_INFO "Setting GPIO pin %d to direction %d\n", pin_data.pin_number, pin_data.direction);

        ret = SetPinDirection(pin_data.pin_number, pin_data.direction);

        if (SUCCESS != ret)
        {
            return ret;
        }

        break;

    case GPIO_SET_PIN_STATE:
        // Set the GPIO pin state (high/low)
        printk(KERN_INFO "Setting GPIO pin %d to state %d\n", pin_data.pin_number, pin_data.state);

        if (flickers[pin_data.pin_number].is_enabled)
        {
            kthread_stop(flicker_threads[pin_data.pin_number]);
            flicker_threads[pin_data.pin_number] = NULL;
        }
        ret = SetPinState(pin_data.pin_number, pin_data.state);
        if (SUCCESS != ret)
        {
            return ret;
        }

        break;

    case GPIO_START_FLICKER:
        // Start flickering the GPIO pin
        printk(KERN_INFO "Starting flicker on GPIO pin %d with speed %d ms\n", pin_data.pin_number, pin_data.flicker_speed);
        ret = StartFlicker(pin_data.pin_number, pin_data.flicker_speed);
        if (SUCCESS != ret)
        {
            return ret;
        }

        break;

    default:
        return -ENOTTY;  // Invalid command
    }

    return 0;
}

// (input or output)
int SetPinDirection(unsigned int pin_number, unsigned int direction)
{
    unsigned int reg_offset = 0; // Offset For The Relevant GPFSEL  
    unsigned int bit_shift = 0;   // Relevant Bits For Specific Pin 
    volatile unsigned int* reg = NULL; // Pointer To the Relevant GPFSEL
    unsigned int val = 0; // GPFSEL Value
    unsigned int read_back = 0; // For Error Handling

    if (pin_number > 53)
    {
        printk(KERN_ALERT "GPIO: invalid Pin Number\n");
        return -EINVAL; // Invalid argument error code

    }

    if (direction > 1)
    {
        printk(KERN_ALERT "GPIO: Invalid Direction\n");
        return -EINVAL; // Invalid argument error code
    }

    reg_offset = (pin_number / 10) * 4;
    bit_shift = (pin_number % 10) * 3;
    reg = (volatile unsigned int*)((volatile char*)gpio_base_virtual + reg_offset);

    // reading the entire register
    val = readl(reg);
    printk(KERN_INFO "GPIO: Original register value (pin %d): 0x%08X\n", pin_number, val);

    // setting off the relevant bits                 
    val &= ~(0x7 << bit_shift);
    printk(KERN_INFO "GPIO: Cleared register value (pin %d): 0x%08X\n", pin_number, val);

    // setting relevant bits direction (input/output) 
    val |= (direction << bit_shift);
    printk(KERN_INFO "GPIO: Set register value (pin %d, direction %d): 0x%08X\n", pin_number, direction, val);

    // writing back the new val
    writel(val, reg);
    printk(KERN_INFO "GPIO: Wrote to register for pin %d: 0x%08X\n", pin_number, val);
    // memory barrier 
    mb();
    msleep(10);
    //checking that the new value was correctly set
    read_back = readl(reg);
    printk(KERN_INFO "GPIO: Read-back register value for pin %d: 0x%08X\n", pin_number, read_back);

    if (read_back != val)
    {
        printk(KERN_ALERT "GPIO: Failed to write to register, read-back value mismatches (pin %d)\n", pin_number);
        return -EIO;  // I/O error
    }


    return 0;
}

// gpio pin state (high or low)
int SetPinState(unsigned int pin_number, unsigned int state)
{
    unsigned int reg_offset = 0;      // Offset for the relevant GPSET / GPCLR
    volatile unsigned int* reg = NULL;  // Pointer to the relevant register
    unsigned int bit_shift = 0;       // Relevant bits for the specific pin
    unsigned int val = 0;
    if (pin_number > 53)
    {
        printk(KERN_ALERT "GPIO: Invalid Pin Number (only 0-53)\n");
        return -EINVAL;
    }

    if (state > 1)
    {
        printk(KERN_ALERT "GPIO: Invalid State (only 0 or 1)\n");
        return -EINVAL;
    }


    bit_shift = pin_number % 32;
    reg_offset = state == 1 ? GPIO_SET0_OFFSET : GPIO_CLR0_OFFSET;
    reg_offset += (pin_number / 32) * 4;
    reg = (volatile unsigned int*)((volatile char*)gpio_base_virtual + reg_offset);
    val = (1 << (bit_shift));
    writel(val, reg);

    return 0;

}

int StartFlicker(unsigned int pin_number, unsigned int speed_ms)
{
    if (TRUE == flickers[pin_number].is_enabled)
    {
        if (flickers[pin_number].speed_ms == speed_ms)
        {
            printk(KERN_INFO "GPIO: Flicker already enabled with the same speed\n");
            return 0;
        }
        else
        {
            kthread_stop(flicker_threads[pin_number]);
            flicker_threads[pin_number] = NULL;
        }
    }

    flickers[pin_number].is_enabled = TRUE;
    flickers[pin_number].speed_ms = speed_ms;


    flicker_threads[pin_number] = kthread_run(FlickerThread,
        &flickers[pin_number], "flicker_thread_%d", pin_number);

    return 0;

}

static int FlickerThread(void* data)
{
    struct flicker_info* info = (struct flicker_info*)data;
    unsigned int pin_number = info - flickers;
    printk(KERN_INFO "GPIO: Starting thread for pin %u\n", pin_number);

    while (FALSE == kthread_should_stop())
    {
        SetPinState(pin_number, GPIO_HIGH);
        msleep(info->speed_ms);
        SetPinState(pin_number, GPIO_LOW);
        msleep(info->speed_ms);
    }

    info->is_enabled = FALSE;
    info->speed_ms = 0;
    printk(KERN_INFO "GPIO:FlickerThread: Exiting for pin %d\n", pin_number);
    return 0;
}
// open and release functions

static int GPIOOpen(struct inode* inode, struct file* file)
{

    if (atomic_cmpxchg(&gpio_device_opened, 0, 1) != 0) {
        return -EBUSY;
    }
    printk(KERN_INFO "GPIO device opened\n");

    return 0;
}

static int GPIORelease(struct inode* inode, struct file* file)
{
    atomic_set(&gpio_device_opened, 0);
    printk(KERN_INFO "GPIO device closed\n");
    return 0;
}


// init and exit funcs

static int __init gpio_init(void)
{

    // major_num = register_chrdev(0, DEVICE_NAME, &gpio_fops);
    // if (major_num < 0)
    // {
        //     iounmap(gpio_base_virtual);
        //     printk(KERN_ALERT "GPIO: Failed to register device, error %d\n", major_num);
        //     return major_num;
        // }


    int major_num = 0;
    int ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    gpio_base_virtual = ioremap(GPIO_BASE_ADDR, GPIO_REGS_SIZE);
    if (gpio_base_virtual == NULL)
    {
        printk(KERN_ALERT "GPIO: Failed to map GPIO registers to virtual memory\n");
        return -ENOMEM;
    }

    if (ret < 0)
    {
        printk(KERN_ALERT "GPIO: Unable to register device\n");
        return ret;
    }
    major_num = MAJOR(dev);
    printk(KERN_INFO "GPIO: major number = %d\n", major_num);

    cdev_init(&gpio_cdev, &gpio_fops);
    gpio_cdev.owner = THIS_MODULE;
    ret = cdev_add(&gpio_cdev, dev, 1);
    if (ret < 0)
    {
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "GPIO: Failed to add cdev\n");
        return ret;
    }




    gpio_class = class_create(DEVICE_NAME);
    if (IS_ERR(gpio_class))
    {
        cdev_del(&gpio_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "GPIO: Failed to create class\n");
        return PTR_ERR(gpio_class);
    }

    // Create device node in /dev/
    gpio_device = device_create(gpio_class, NULL, dev, NULL, DEVICE_NAME);
    if (IS_ERR(gpio_device))
    {
        class_destroy(gpio_class);
        cdev_del(&gpio_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "GPIO: Failed to create device\n");
        return PTR_ERR(gpio_device);
    }

    printk(KERN_INFO "GPIO: Device registered with major number %d\n", major_num);

    return 0;
}

static void __exit gpio_exit(void)
{
    int i = 0;

    // Stop any active flicker threads
    for (; i < MAX_GPIO_PINS; i++)
    {
        if (NULL != flicker_threads[i])
        {
            kthread_stop(flicker_threads[i]);
            flicker_threads[i] = NULL;
            printk(KERN_INFO "GPIO:Stopped flicker thread on pin %d\n", i);

        }
    }

    unregister_chrdev_region(dev, 1);  // 1 = NUMBER OF MINOR NUMS e.g devices that uses the device driver

    if (NULL != gpio_base_virtual)
    {
        iounmap(gpio_base_virtual);
    }
    device_destroy(gpio_class, dev);
    class_destroy(gpio_class);
    cdev_del(&gpio_cdev);
    printk(KERN_INFO "GPIO driver exited\n");
}

module_init(gpio_init);
module_exit(gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raz");
MODULE_DESCRIPTION("gpio driver for raspberry pi");
































