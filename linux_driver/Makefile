CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Werror
CPPFLAGS += -I../include

all: ledctl

KDIR ?= /lib/modules/$(shell uname -r)/build

obj-m += usb_led.o

module:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

ledctl: ledctl.c ../include/led_protocol.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ ledctl.c

clean:
	rm -f ledctl

module-clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

.PHONY: all module clean module-clean
