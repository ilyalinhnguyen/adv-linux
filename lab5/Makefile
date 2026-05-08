CC := cc
CFLAGS := -std=c11 -O2 -Wall -Wextra -pedantic
USER_TARGET := kernel_stack
USER_SRC := kernel_stack.c
KMOD_SRC := usb_int_stack.c

KDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: all user module clean

all: user module

user: $(USER_TARGET)

$(USER_TARGET): $(USER_SRC) int_stack_ioctl.h
	$(CC) $(CFLAGS) -o $@ $(USER_SRC)

module: $(KMOD_SRC) int_stack_ioctl.h
	$(MAKE) -C $(KDIR) M=$(PWD) modules

obj-m += usb_int_stack.o

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f $(USER_TARGET)
