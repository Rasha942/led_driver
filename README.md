# Raspberry Pi GPIO Character Driver

A Linux **kernel character device driver** for the Raspberry Pi GPIO controller,
written against the bare BCM283x registers (no `gpiolib` helpers), plus a
user-space test program that drives it through `ioctl`.

Target hardware: **Raspberry Pi 2 / 3** (BCM2836/BCM2837, peripheral base
`0x3F000000`). For a Pi 1 / Zero use base `0x20000000`; for a Pi 4 use
`0xFE000000` — see `include/gpio_kernel.h`.

## Features

- **Char device** created with the modern API: `alloc_chrdev_region` +
  `cdev_init`/`cdev_add` + `class_create` + `device_create`
  (auto-creates `/dev/gpio_driver`).
- **Direct MMIO register access** (`ioremap`, `readl`/`writel`, memory barriers)
  to set pin **direction** (GPFSEL) and **state** (GPSET/GPCLR), with read-back
  verification.
- **LED flicker / blink** using kernel **timers**, backed by a **slab cache**
  (`kmem_cache`) and a linked list of active pins.
- **`ioctl` control interface** with magic-number command macros
  (`include/gpio_ioctl.h`).
- **`read`/`write`** backed by a ring buffer with IRQ-safe **spinlock** locking.
- A real **GPIO interrupt** handler (`request_irq`, rising-edge detect via
  GPREN/GPEDS registers).
- Single-opener enforcement via `atomic_cmpxchg`.
- Fully unwound error handling in module init.

## Layout

```
include/gpio_ioctl.h    Shared ioctl ABI (commands + struct gpio_pin)
include/gpio_kernel.h   Register offsets, addresses and driver constants
src/gpio_kernel.c       The kernel module
src/user_prog.c         Interactive user-space test program
Makefile                Builds the module and the user program
```

## Build

On the Pi (with kernel headers installed):

```sh
make            # builds the kernel module (src/gpio_kernel.ko) and user_prog
make module     # module only
make user       # user program only
make clean
```

Cross-compiling from a host:

```sh
make module KDIR=/path/to/rpi-linux ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf-
```

## Run

```sh
sudo insmod src/gpio_kernel.ko     # /dev/gpio_driver appears
sudo ./user_prog                   # follow the prompts (pin, direction, state, flicker)
dmesg | tail                       # driver logs
sudo rmmod gpio_kernel
```

> The IRQ demo uses GPIO 27 (IRQ 49) — see `GPIO_IRQ_PIN` / `GPIO_IRQ_NUM`.

## Notes / limitations

- Physical addresses are board-specific (see above). The driver **must** run on a
  Pi — loading it on a different machine is meaningless and unsafe, since it maps
  fixed BCM283x peripheral addresses.
- Timer API names differ across kernel versions; the driver shims
  `del_timer_sync`/`from_timer` so it builds on both older Pi kernels and recent
  mainline (see the top of `src/gpio_kernel.c`).
- `GPIO_KGDB_TEST` is a small helper kept as a `kgdb` breakpoint target.
