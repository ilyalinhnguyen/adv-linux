#ifndef INT_STACK_IOCTL_H
#define INT_STACK_IOCTL_H

#include <linux/ioctl.h>

#define INT_STACK_IOCTL_MAGIC 'k'
#define INT_STACK_IOCTL_SET_SIZE _IOW(INT_STACK_IOCTL_MAGIC, 1, int)

#define INT_STACK_DEVICE_PATH "/dev/int_stack"

#endif
