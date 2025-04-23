
#ifndef __GPIO_IOCTL_H__
#define __GPIO_IOCTL_H__

#include <linux/ioctl.h>  // For defining IOCTL macros

#define GPIO_IOCTL_MAGIC_NUM  'Z' 

// output/input

#define GPIO_INPUT 0
#define GPIO_OUTPUT 1

// on/off
#define GPIO_LOW   0        
#define GPIO_HIGH  1


typedef struct gpio_pin
{
    int pin_number;
    int direction;
    int state;
}gpio_pin;

#define GPIO_SET_PIN_DIR  _IOW(GPIO_IOCTL_MAGIC_NUM, 1, struct gpio_pin)
#define GPIO_SET_PIN_STATE _IOW(GPIO_IOCTL_MAGIC_NUM, 2, struct gpio_pin)

long gpio_ioctl(struct file *file, unsigned int cmd, unsigned long arg);


#endif //__GPIO_IOCTL_H__




