#ifndef __GPIO_KERNEL_H__
#define __GPIO_KERNEL_H__

#include "gpio_ioctl.h"

//general macros
#define SUCCESS 0
#define TRUE 1
#define FALSE 0

//ioctl macros
#define DRIVER_NAME "gpio_driver"
#define BUFFER_SIZE 1024
// static DEFINE_SPINLOCK(spinlock);

//ARM interrupt macros 

#define IRQ_ENABLE1_ADDR 0x3F000214


// GPIO macros

#define MAX_GPIO_PINS    54
#define GPIO_IRQ_PIN 27
#define GPIO_IRQ_NUM 49
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

// offsets for GPIO Pull-up/down Register & GPIO Pull-up/down Clock Registers (GPPUDCLKn) 
#define GPIO_GPPUD       0x94
#define GPIO_GPPUDCLK0   0x98
#define GPIO_GPPUDCLK1   0x9C        

// Offset For GPIO Pin Event Detect
#define GPIO_GPEDS0  0x40 // pins 0-31
// Offset For GPIO Pin Rising Edge Detect
#define GPIO_GPREN0 0x4C// Pins 0-31
#endif // __GPIO_KERNEL_H__