#include <Arduino.h>
#include "USB.h"
#include "USBVendor.h"
#include "esp32-hal-tinyusb.h"
#include "led_protocol.h"

namespace {
constexpr uint8_t kLedPins[LED_COUNT] = {16, 17, 18};
USBVendor usbDevice;
uint8_t ledStates[LED_COUNT] = {0, 0, 0};
uint8_t request[LED_FRAME_SIZE];
size_t requestLength = 0;
uint32_t lastByteAt = 0;

void respond(uint8_t status, uint8_t led, uint8_t state) {
    const uint8_t response[LED_FRAME_SIZE] = {
        LED_RESPONSE_MAGIC, status, led, state
    };
    const uint32_t started = millis();
    while (usbDevice.mounted() && tud_vendor_write_available() < sizeof(response) &&
           millis() - started < 100) {
        delay(1);
    }
    if (usbDevice.mounted() &&
        usbDevice.write(response, sizeof(response)) == sizeof(response)) {
        tud_vendor_write_flush();
    }
}

void handleRequest() {
    const uint8_t command = request[1];
    const uint8_t led = request[2];
    const uint8_t value = request[3];

    if (command != LED_CMD_SET && command != LED_CMD_GET) {
        respond(LED_STATUS_BAD_COMMAND, led, 0);
        return;
    }
    if (led > LED_COUNT || (command == LED_CMD_GET && led == LED_ALL)) {
        respond(LED_STATUS_BAD_LED, led, 0);
        return;
    }
    if (command == LED_CMD_SET && value > 1) {
        respond(LED_STATUS_BAD_VALUE, led, 0);
        return;
    }
    if (command == LED_CMD_GET) {
        respond(LED_STATUS_OK, led, ledStates[led - 1]);
        return;
    }

    const uint8_t first = led == LED_ALL ? 0 : led - 1;
    const uint8_t last = led == LED_ALL ? LED_COUNT : led;
    for (uint8_t i = first; i < last; ++i) {
        digitalWrite(kLedPins[i], value ? HIGH : LOW);
        ledStates[i] = value;
    }
    respond(LED_STATUS_OK, led, value);
}
} // namespace

void setup() {
    for (uint8_t pin : kLedPins) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    usbDevice.begin();
    USB.VID(0x303A);
    USB.PID(0x1001);
    USB.productName("ESP32-S3 LED Device");
    USB.usbClass(0xff);
    USB.usbSubClass(0);
    USB.usbProtocol(0);
    USB.begin();
}

void loop() {
    if (requestLength != 0 && millis() - lastByteAt > 250) {
        requestLength = 0;
    }
    while (usbDevice.available() > 0) {
        const int received = usbDevice.read();
        if (received < 0) break;
        const uint8_t byte = static_cast<uint8_t>(received);
        if (requestLength == 0 && byte != LED_REQUEST_MAGIC) continue;
        request[requestLength++] = byte;
        lastByteAt = millis();
        if (requestLength == LED_FRAME_SIZE) {
            handleRequest();
            requestLength = 0;
        }
    }
    delay(1);
}
