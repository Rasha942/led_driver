//file 1 in user space
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>  
#include <sys/ioctl.h>
#include "gpio_ioctl.h"  

// Function to handle user input for selecting pin direction and state
void set_gpio_pin(int fd, int pin_number)
{
    gpio_pin pin;
    pin.pin_number = pin_number;
    pin.flicker_speed = 0;
    // user choose direction of pin (input or output)
    printf("Enter direction for GPIO pin %d (0 for input, 1 for output): ", pin_number);
    scanf("%d", &pin.direction);

    // Send ioctl command to set pin direction
    if (ioctl(fd, GPIO_SET_PIN_DIR, &pin) == -1)
    {
        perror("Failed to set pin direction");
        return;
    }
    // If output, user choose state of pin (high or low)
    if (pin.direction == GPIO_OUTPUT)
    {
        printf("Enter state for GPIO pin %d (0 for LOW, 1 for HIGH): ", pin_number);
        scanf("%d", &pin.state);
        if (GPIO_HIGH == pin.state)
        {


            printf("Choose flicker speed in ms (for no flicker enter 0):\n");
            scanf("%d", &pin.flicker_speed);
        }
    }

    if (0 == pin.flicker_speed)
    {
        // Send ioctl command to set pin state
        if (ioctl(fd, GPIO_SET_PIN_STATE, &pin) == -1)
        {
            perror("Failed to set pin state");
            return;
        }
    }
    else
    {
        // Send ioctl command to start flickering
        if (ioctl(fd, GPIO_START_FLICKER, &pin) == -1)
        {
            perror("Failed to start flickering");
            return;
        }
    }

    printf("GPIO pin %d direction and state set successfully.\n", pin_number);
}


int main()
{
    int fd = 0, pin_number = 0;

    fd = open("/dev/gpio_device", O_RDWR);
    if (fd == -1)
    {
        perror("Failed to open GPIO device");
        return -1;
    }

    //user chooses pin number
    printf("Enter the GPIO pin number you want to control (0-53): ");
    scanf("%d", &pin_number);

    // Set the GPIO pin direction and state
    set_gpio_pin(fd, pin_number);

    // Close the device file
    close(fd);

    return 0;
}
