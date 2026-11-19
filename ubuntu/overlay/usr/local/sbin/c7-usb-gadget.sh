#!/bin/sh
# The postmarketOS initramfs creates a composite USB gadget ("g1") with a
# network function only.  Rebind it with a CDC-ACM function added so the phone
# also exposes /dev/ttyGS0 as a serial console - the display may not come up on
# a fresh port, and then this is the only way in besides ssh.
set -eu

CFG=/sys/kernel/config/usb_gadget/g1
UDC=$(cat "$CFG/UDC" 2>/dev/null || true)

[ -d "$CFG" ] || exit 0
[ -e /dev/ttyGS0 ] && exit 0
[ -d "$CFG/functions/acm.usb0" ] && exit 0

echo "adding acm.usb0 to the USB gadget"
[ -n "$UDC" ] && echo "" > "$CFG/UDC"
mkdir -p "$CFG/functions/acm.usb0"
ln -sf "$CFG/functions/acm.usb0" "$CFG/configs/c.1/acm.usb0"
[ -n "$UDC" ] && echo "$UDC" > "$CFG/UDC"
exit 0
