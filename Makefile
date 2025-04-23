# Path to kernel makefile
KDIR := /lib/modules/$(shell uname -r)/build  # Get current kernel build directory

# Path to current directory (project root)
PWD := $(shell pwd)

# Name of the kernels module 
obj-m := gpio_kernel.o

# Path to include directory 
ccflags-y := W-I$(PWD)/include  

# Default target
# -C $(KDIR): switch to kernel makefile directory
# M=$(PWD)/src: tells the kernel makefile to look source files in src
# modules: a target in the kernel makefile  for modules
all:
	make -C $(KDIR) M=$(PWD)/src modules

clean:
	make -C $(KDIR) M=$(PWD)/src clean
