# Path to kernel build directory (defaults to the running kernel).
# Cross-compiling for a Raspberry Pi, override e.g.:
#   make KDIR=~/linux ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
KDIR := /lib/modules/$(shell uname -r)/build

# Path to current directory (project root)
PWD := $(shell pwd)

# Name of the kernel module
obj-m := gpio_kernel.o

# Path to include directory
ccflags-y := -I$(PWD)/include

# User-space test program
CC      ?= gcc
CFLAGS  ?= -Wall -Iinclude

# Default target: kernel module + user program
all: module user

# -C $(KDIR): switch to kernel build directory
# M=$(PWD)/src: tells the kernel build to look for source files in src
# modules: the kernel build target for out-of-tree modules
module:
	$(MAKE) -C $(KDIR) M=$(PWD)/src modules

user: src/user_prog.c
	$(CC) $(CFLAGS) -o user_prog src/user_prog.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD)/src clean
	rm -f user_prog

.PHONY: all module user clean
