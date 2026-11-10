"""Control the ESP32-S3 LED device through WinUSB and libusb."""

import argparse
import sys

try:
    import libusb_package
    import usb.core
    import usb.util
except ImportError as exc:
    raise SystemExit(
        f"Missing Python package: {exc.name}. Run: py -m pip install pyusb libusb-package"
    ) from exc


VID = 0x303A
PID = 0x1001
PRODUCT = "ESP32-S3 LED Device"
REQUEST_MAGIC = 0xA5
RESPONSE_MAGIC = 0x5A
STATUS_NAMES = {1: "invalid command", 2: "invalid LED", 3: "invalid value"}
TIMEOUT_MS = 2000


def find_device(backend):
    matches = []
    for device in usb.core.find(
        find_all=True, idVendor=VID, idProduct=PID, backend=backend
    ):
        try:
            if usb.util.get_string(device, device.iProduct) == PRODUCT:
                matches.append(device)
        except usb.core.USBError:
            continue
    if not matches:
        raise RuntimeError(
            "ESP32-S3 LED Device (303A:1001) not found. Check the USB connection "
            "and confirm that WinUSB is installed for this device."
        )
    if len(matches) != 1:
        raise RuntimeError("Multiple matching devices found; connect one at a time.")
    return matches[0]


def find_endpoints(device):
    # Windows normally configures the device during enumeration. Reconfiguring an
    # already active WinUSB device is unnecessary and may fail on Windows.
    config = device.get_active_configuration()
    for interface in config:
        if interface.bInterfaceClass != 0xFF:
            continue
        bulk_in = None
        bulk_out = None
        for endpoint in interface:
            if usb.util.endpoint_type(endpoint.bmAttributes) != usb.util.ENDPOINT_TYPE_BULK:
                continue
            if usb.util.endpoint_direction(endpoint.bEndpointAddress) == usb.util.ENDPOINT_IN:
                bulk_in = endpoint.bEndpointAddress
            else:
                bulk_out = endpoint.bEndpointAddress
        if bulk_in is not None and bulk_out is not None:
            return bulk_in, bulk_out
    raise RuntimeError("Vendor interface with bulk IN/OUT endpoints not found.")


def main():
    parser = argparse.ArgumentParser(description="Control GPIO16, GPIO17, GPIO18 over USB")
    subcommands = parser.add_subparsers(dest="command", required=True)
    set_parser = subcommands.add_parser("set", help="set one or all outputs")
    set_parser.add_argument("led", choices=("1", "2", "3", "all"))
    set_parser.add_argument("state", choices=("on", "off"))
    get_parser = subcommands.add_parser("get", help="read one output state")
    get_parser.add_argument("led", choices=("1", "2", "3"))
    args = parser.parse_args()

    backend = libusb_package.get_libusb1_backend()
    if backend is None:
        raise RuntimeError("libusb backend not found; reinstall libusb-package for this Python architecture.")

    device = find_device(backend)
    try:
        bulk_in, bulk_out = find_endpoints(device)
        led = 0 if args.led == "all" else int(args.led)
        command = 1 if args.command == "set" else 2
        value = int(args.state == "on") if command == 1 else 0
        request = bytes((REQUEST_MAGIC, command, led, value))
        written = device.write(bulk_out, request, timeout=TIMEOUT_MS)
        if written != len(request):
            raise RuntimeError(f"Short USB write: {written} of {len(request)} bytes")
        response = bytes(device.read(bulk_in, 4, timeout=TIMEOUT_MS))
        if len(response) != 4 or response[0] != RESPONSE_MAGIC or response[2] != led:
            raise RuntimeError(f"Invalid device response: {response.hex(' ')}")
        if response[1] != 0:
            raise RuntimeError(STATUS_NAMES.get(response[1], f"device status {response[1]}"))
        if response[3] not in (0, 1):
            raise RuntimeError(f"Invalid output state: {response[3]}")
        print(f"{'all' if led == 0 else f'GPIO{15 + led}'}: {'on' if response[3] else 'off'}")
    finally:
        usb.util.dispose_resources(device)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, usb.core.USBError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)
