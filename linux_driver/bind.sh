#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 || $EUID -ne 0 ]]; then
    echo "Usage: sudo $0 <USB data interface, e.g. 1-2:1.0>" >&2
    exit 2
fi
iface=$1
data=/sys/bus/usb/devices/$iface
[[ -d $data && $iface =~ ^[0-9]+(-[0-9.]+)?:[0-9]+\.[0-9]+$ ]] || {
    echo "Invalid USB interface: $iface" >&2
    exit 2
}
device=/sys/bus/usb/devices/${iface%%:*}
[[ $(cat "$device/idVendor") == 303a &&
   $(cat "$device/idProduct") == 1001 &&
   $(cat "$device/product") == 'ESP32-S3 LED Device' &&
   $(cat "$data/bInterfaceClass") == ff ]] || {
    echo "Interface does not belong to this LED device" >&2
    exit 2
}
[[ -d /sys/bus/usb/drivers/usb_led ]] || {
    insmod "$(dirname "$0")/usb_led.ko"
}
if [[ -L $data/driver &&
      $(basename "$(readlink "$data/driver")") == usb_led ]]; then
    echo "Already bound: $iface"
else
    printf '%s' usb_led > "$data/driver_override"
    printf '%s' "$iface" > /sys/bus/usb/drivers/usb_led/bind
fi
for class_device in /sys/class/usbmisc/usb_led*; do
    [[ -f $class_device/dev ]] || continue
    [[ $(readlink -f "$class_device/device") == $(readlink -f "$data") ]] || continue
    node=/dev/$(basename "$class_device")
    if [[ ! -e $node ]]; then
        IFS=: read -r major minor < "$class_device/dev"
        mknod "$node" c "$major" "$minor"
    fi
    echo "Bound $iface as $node ($(cat "$class_device/dev"))"
    exit 0
done
echo "Driver bound, but no usbmisc device was registered; check dmesg" >&2
exit 1
