# Linux USB character driver

`usb_led.c` is a kernel USB driver for the ESP32-S3 LED device. It binds to
the vendor-specific interface, discovers its bulk endpoints, and registers a USB
character device with `usb_register_dev`. Linux assigns USB major 180. The
minor starts at 192 unless `CONFIG_USB_DYNAMIC_MINORS` is enabled; in that
case the USB core allocates it dynamically. The device appears as
`/dev/usb_led0` (or another available number). The driver handles open,
close, four-byte read/write,
disconnect and module removal. Only one process can open a device at once.

## Build

On the target Linux system, install headers for the running kernel and a C
compiler, then run:

```sh
make -C linux_driver module
make -C linux_driver
```

## Bind the device

The firmware exposes one vendor-specific interface with bulk IN and OUT
endpoints. Linux can match `usb_led` directly; `cdc_acm` is not involved.
The module's USB ID table matches `303a:1001` and allows automatic binding
when the module is installed and loaded. For a manually built module,
`bind.sh` checks the device identity and binds its interface. If this Linux
system does not populate `/dev` automatically, the script creates the node
using the actual major and minor numbers reported by sysfs.

Find the interface under `/sys/bus/usb/devices` (its
`bInterfaceClass` file contains `ff`). For example, if it is `1-2:1.0`:

```sh
sudo bash linux_driver/bind.sh 1-2:1.0
cat /sys/class/usbmisc/usb_led0/dev
ls -l /dev/usb_led0
linux_driver/ledctl /dev/usb_led0 set 1 on
linux_driver/ledctl /dev/usb_led0 get 1
linux_driver/ledctl /dev/usb_led0 set all off
```

The `dev` sysfs file reports the assigned `major:minor`. Depending on the
distribution's device manager, the class path may instead be under
`/sys/class/usb`. Use `stat -c '%t:%T' /dev/usb_led0` to inspect the node.
The account running `ledctl` needs permission to open the node. If the module
is not installed for automatic loading, re-run the bind script after
reconnecting the board.

## Protocol

Each request is `A5 command led value`; each response is
`5A status led state`. Command 1 sets and command 2 gets a LED. LED 1–3
select a single LED; LED 0 selects all three for set only. Value is 0 or 1.
Status 0 means success; 1, 2, 3 mean invalid command, LED and value.
The node accepts one complete four-byte request per `write` and returns one
complete four-byte response per `read`.
