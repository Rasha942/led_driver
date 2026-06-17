ifneq ($(KERNELRELEASE),)
# ---- kbuild pass: invoked recursively by the kernel build system ----
# $(src) is this Makefile's directory, so headers live in $(src)/include.
obj-m := gpio_kernel.o
gpio_kernel-y := src/gpio_kernel.o
ccflags-y := -I$(src)/include

else
# ---- normal pass: invoked directly by the user ----
# Kernel build directory (defaults to the running kernel). Cross-compiling for a
# Raspberry Pi, override e.g.:
#   make KDIR=~/linux ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
KDIR := /lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

# User-space test program
CC     ?= gcc
CFLAGS ?= -Wall -Iinclude

# Default target: kernel module + user program
all: module user

module:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

user: src/user_prog.c
	$(CC) $(CFLAGS) -o user_prog src/user_prog.c

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f user_prog

.PHONY: all module user clean

endif
