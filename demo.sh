#!/usr/bin/env bash
#
# demo.sh - Proof-of-concept demo for the Raspberry Pi GPIO character driver.
#
# IMPORTANT: this exercises real hardware, so it must run ON a Raspberry Pi 2/3
# (BCM283x), as root. The driver maps fixed BCM283x peripheral addresses, so on
# any other machine `insmod` will fail by design -- there the portable proof is
# simply that the code builds (`make`).
#
# Wire an LED (+ resistor) between the chosen BCM GPIO pin and ground.
#
# Usage:  sudo ./demo.sh [BCM_GPIO_PIN]   (default pin: 17)
#
set -euo pipefail

MODULE=gpio_kernel
KO=gpio_kernel.ko
DEV=/dev/gpio_driver
LED_PIN=${1:-17}     # BCM GPIO pin wired to an LED
BLINK_MS=500         # flicker half-period in milliseconds

log()  { printf '\n\033[1;36m== %s ==\033[0m\n' "$*"; }
warn() { printf '\033[1;33mWARNING:\033[0m %s\n' "$*"; }
fail() { printf '\033[1;31mERROR:\033[0m %s\n' "$*" >&2; exit 1; }

# user_prog is interactive; feed it the menu answers on stdin.
#   args: pin direction state flicker_ms   (test-type is always 1 = LED)
drive() {
    printf '%s\n1\n%s\n%s\n%s\n' "$1" "$2" "$3" "$4" | ./user_prog
}

# 1. sanity checks ----------------------------------------------------------
[ "$(id -u)" -eq 0 ] || fail "run as root: sudo ./demo.sh"

if ! grep -qi raspberry /proc/device-tree/model 2>/dev/null \
   && ! grep -qi raspberry /proc/cpuinfo 2>/dev/null; then
    warn "this does not look like a Raspberry Pi."
    warn "the module maps BCM283x addresses and will fail to load here."
    warn "on a non-Pi machine, 'make' alone is the proof that the code builds."
fi

# 2. build ------------------------------------------------------------------
log "Building kernel module + user program"
make
[ -f "$KO" ] || fail "$KO was not built"

# 3. load -------------------------------------------------------------------
log "Loading module ($MODULE)"
if lsmod | grep -q "^$MODULE\b"; then rmmod "$MODULE"; fi
insmod "$KO" || fail "insmod failed (expected on non-Pi hardware)"
[ -e "$DEV" ] || fail "$DEV was not created"
ls -l "$DEV"

# 4. blink ------------------------------------------------------------------
log "Blinking GPIO $LED_PIN every ${BLINK_MS}ms for 5 seconds"
drive "$LED_PIN" 1 1 "$BLINK_MS"   # direction=out, state=high, flicker=BLINK_MS
sleep 5                            # the kernel timer keeps blinking after exit

# 5. solid on, then off (setting direction also cancels the flicker timer) ---
log "Solid ON for 1 second"
drive "$LED_PIN" 1 1 0             # state=high, no flicker
sleep 1
log "Turning OFF"
printf '%s\n1\n1\n0\n' "$LED_PIN" | ./user_prog   # direction=out, state=low

# 6. kernel log -------------------------------------------------------------
log "Recent driver messages (dmesg)"
dmesg | grep -i "$MODULE" | tail -n 15 || true

# 7. unload -----------------------------------------------------------------
log "Unloading module"
rmmod "$MODULE"

log "Demo complete"
