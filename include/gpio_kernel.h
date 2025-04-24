#ifndef __GPIO_KERNEL_H__
#define __GPIO_KERNEL_H__

#include "gpio_ioctl.h"

//general macros
#define SUCCESS 0
#define TRUE 1
#define FALSE 0




#define MAX_GPIO_PINS    54
#define GPIO_BASE_ADDR   0x3F200000  
#define GPIO_REGS_SIZE   0xB4
// offset for GPIO Function Select        
#define GPFSEL0_OFFSET    0x00    // pins 0-9
#define GPFSEL1_OFFSET    0x04   // pins 10-19
#define GPFSEL2_OFFSET    0x08   // pins 20 - 29
#define GPFSEL3_OFFSET    0x0C   //  pins 30 - 39
#define GPFSEL4_OFFSET    0x10   // pins 40 - 49
#define GPFSEL5_OFFSET    0x14   // pins 50 - 53

// offsetset/clear registers
#define GPIO_SET0_OFFSET      0x1C      // pins 0-31
#define GPIO_SET1_OFFSET      0x20     //  pins 32-53  
#define GPIO_CLR0_OFFSET      0x28    // pins 0-31    
#define GPIO_CLR1_OFFSET      0x2C   // pins 32-53


//ioctl macros
#define DEVICE_NAME "gpio_device"



#endif // __GPIO_KERNEL_H__