#ifndef LED_PROTOCOL_H
#define LED_PROTOCOL_H

/* Each request and response is exactly four bytes. */
#define LED_REQUEST_MAGIC 0xA5
#define LED_RESPONSE_MAGIC 0x5A
#define LED_FRAME_SIZE 4

#define LED_CMD_SET 1
#define LED_CMD_GET 2

#define LED_STATUS_OK 0
#define LED_STATUS_BAD_COMMAND 1
#define LED_STATUS_BAD_LED 2
#define LED_STATUS_BAD_VALUE 3

/* LED 0 addresses all three LEDs for SET; GET requires LED 1..3. */
#define LED_ALL 0
#define LED_COUNT 3

#endif
