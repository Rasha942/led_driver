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
#include "gpio_kernel.h"     //  ioctl definitions






// GLOBAL VARS & FORWARD DECLERATIONS //

int SetPinDirection(unsigned int pin_number, unsigned int direction);
int SetPinState(unsigned int pin_number, unsigned int state);
long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static int GPIOOpen(struct inode *inode, struct file *file);
static int GPIORelease(struct inode *inode, struct file *file);


static dev_t dev = 0;
static struct cdev gpio_cdev;
static struct class* gpio_class;
static struct device* gpio_device;
static int gpio_device_opened = 0;
static void __iomem* gpio_base_virtual = NULL;

static const struct file_operations gpio_fops = 
{
    .owner = THIS_MODULE,
    .unlocked_ioctl = gpio_ioctl,
    .open = GPIOOpen,
    .release = GPIORelease, 
};

////////////////////////

long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    struct gpio_pin pin_data;

    switch (cmd) {
        case GPIO_SET_PIN_DIR:
            // Copy data from user space to kernel space
            ret = copy_from_user(&pin_data, (struct gpio_pin __user *)arg, sizeof(pin_data));
            if (SUCCESS != ret)
            {
                return -EFAULT;
            }

            printk(KERN_INFO "Setting GPIO pin %d to direction %d\n", pin_data.pin_number, pin_data.direction);
            // call relevant function
            ret = SetPinDirection(pin_data.pin_number, pin_data.direction);
            if (SUCCESS != ret)
            {
                return ret;
            }

            break;

        case GPIO_SET_PIN_STATE:
            // Copy data from user space to kernel space
            ret = copy_from_user(&pin_data, (struct gpio_pin __user *)arg, sizeof(pin_data));
            if (SUCCESS != ret)
                return -EFAULT;

            // Set the GPIO pin state (high/low)
            printk(KERN_INFO "Setting GPIO pin %d to state %d\n", pin_data.pin_number, pin_data.state);

            ret = SetPinState(pin_data.pin_number, pin_data.state);
            if(SUCCESS != ret)
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
    reg = (volatile unsigned int*)((volatile char *)gpio_base_virtual + reg_offset);

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
    //cheking that the new value was correctly set
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
int SetPinState(unsigned int pin_number,unsigned int state)
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
    reg_offset = state == 1 ? GPIO_SET0_OFFSET: GPIO_CLR0_OFFSET;
    reg_offset += (pin_number / 32) * 4;
    reg = (volatile unsigned int *)((volatile char*)gpio_base_virtual + reg_offset);
    val = (1 << (bit_shift));
    writel(val, reg);  

    return 0;
    
}

// open and release functions

static int GPIOOpen(struct inode *inode, struct file *file)
{
    
    if (TRUE == gpio_device_opened)
    {
        return -EBUSY;  
    }
    printk(KERN_INFO "GPIO device opened\n");

    gpio_device_opened = TRUE;  // Mark device as open
    return 0;
}

static int GPIORelease(struct inode *inode, struct file *file)
{
    printk(KERN_INFO "GPIO device closed\n");

    gpio_device_opened = FALSE;  // Mark device as closed
    return 0;
}


// init and exit funcs

static int __init gpio_init(void)
{
    int major_num = 0;
    int ret = alloc_chrdev_region(&dev, 0, 1, DEVICE_NAME);
    
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


    // Memory map the GPIO registers bus addresses to virtual addresses 
    gpio_base_virtual = ioremap(GPIO_BASE_ADDR, 80);  // 40 = size of used registers
    if (NULL == gpio_base_virtual) 
    {
        cdev_del(&gpio_cdev);
        unregister_chrdev_region(dev, 1);
        printk(KERN_ALERT "GPIO: Failed to map GPIO registers to virtual memory\n");
        return -ENOMEM;
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

    printk(KERN_INFO "GPIO driver initialized\n");

    return 0;
}

static void __exit gpio_exit(void)
{
 
    if (NULL != gpio_base_virtual) 
    {
        iounmap(gpio_base_virtual);
    }
    device_destroy(gpio_class, dev);
    class_destroy(gpio_class);
    cdev_del(&gpio_cdev);
    unregister_chrdev_region(dev, 1);  // 1 = NUMBER OF MINOR NUMS e.g devices that uses the device driver
    printk(KERN_INFO "GPIO driver exited\n");
}

module_init(gpio_init);
module_exit(gpio_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Raz");
MODULE_DESCRIPTION("gpio driver for raspberry pi");
































