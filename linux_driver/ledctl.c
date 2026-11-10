#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "led_protocol.h"

static int64_t now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return -1;
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int transfer(int fd, uint8_t *data, size_t length, short event,
                    int64_t deadline) {
    size_t done = 0;
    while (done < length) {
        int64_t remaining = deadline - now_ms();
        if (remaining <= 0) { errno = ETIMEDOUT; return -1; }
        struct pollfd pfd = {.fd = fd, .events = event};
        int ready = poll(&pfd, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) continue;
        if (ready < 0) return -1;
        if (ready == 0) { errno = ETIMEDOUT; return -1; }
        if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
            errno = EIO; return -1;
        }
        if (!(pfd.revents & event)) continue;
        ssize_t count = event == POLLIN
            ? read(fd, data + done, length - done)
            : write(fd, data + done, length - done);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        if (count < 0) return -1;
        if (count == 0) { errno = EIO; return -1; }
        done += (size_t)count;
    }
    return 0;
}

static int parse_led(const char *text, int allow_all) {
    if (allow_all && strcmp(text, "all") == 0) return LED_ALL;
    if (strlen(text) == 1 && text[0] >= '1' && text[0] <= '3')
        return text[0] - '0';
    return -1;
}

static void usage(const char *program) {
    fprintf(stderr, "Usage: %s /dev/usb_led0 set {1|2|3|all} {on|off}\n"
                    "       %s /dev/usb_led0 get {1|2|3}\n", program, program);
}

int main(int argc, char **argv) {
    int command, led, value = 0;
    if (argc == 5 && strcmp(argv[2], "set") == 0) {
        command = LED_CMD_SET;
        led = parse_led(argv[3], 1);
        if (strcmp(argv[4], "on") == 0) value = 1;
        else if (strcmp(argv[4], "off") != 0) value = -1;
    } else if (argc == 4 && strcmp(argv[2], "get") == 0) {
        command = LED_CMD_GET;
        led = parse_led(argv[3], 0);
    } else {
        usage(argv[0]); return 2;
    }
    if (led < 0 || value < 0) { usage(argv[0]); return 2; }

    int fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) { perror(argv[1]); return 1; }
    uint8_t request[LED_FRAME_SIZE] = {
        LED_REQUEST_MAGIC, (uint8_t)command, (uint8_t)led, (uint8_t)value
    };
    uint8_t response[LED_FRAME_SIZE];
    int64_t deadline = now_ms() + 2000;
    if (transfer(fd, request, sizeof(request), POLLOUT, deadline) < 0) {
        perror("write request"); close(fd); return 1;
    }
    if (transfer(fd, response, sizeof(response), POLLIN, deadline) < 0) {
        perror("read response"); close(fd); return 1;
    }
    close(fd);
    if (response[0] != LED_RESPONSE_MAGIC) {
        fprintf(stderr, "Invalid response marker\n"); return 1;
    }
    if (response[2] != (uint8_t)led) {
        fprintf(stderr, "Unexpected LED in response\n"); return 1;
    }
    if (response[1] != LED_STATUS_OK) {
        fprintf(stderr, "Device error %u\n", response[1]); return 1;
    }
    if (response[3] > 1) {
        fprintf(stderr, "Invalid state in response\n"); return 1;
    }
    if (led == LED_ALL) printf("All LEDs: %s\n", response[3] ? "on" : "off");
    else printf("LED %d: %s\n", led, response[3] ? "on" : "off");
    return 0;
}
