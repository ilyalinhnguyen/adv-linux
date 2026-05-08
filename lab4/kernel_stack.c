#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

#include "int_stack_ioctl.h"

static void print_usage(const char *prog)
{
	fprintf(stderr,
		"Usage:\n"
		"  %s set-size <N>\n"
		"  %s push <INT>\n"
		"  %s pop\n"
		"  %s unwind\n",
		prog, prog, prog, prog);
}

static int parse_int(const char *s, int *out)
{
	long v;
	char *end = NULL;

	errno = 0;
	v = strtol(s, &end, 10);
	if (errno != 0 || end == s || *end != '\0' || v < INT_MIN || v > INT_MAX)
		return -1;

	*out = (int)v;
	return 0;
}

static int open_stack_device(void)
{
	int fd = open(INT_STACK_DEVICE_PATH, O_RDWR);
	if (fd < 0)
		perror("open");
	return fd;
}

static int cmd_set_size(int fd, const char *value_str)
{
	int size;

	if (parse_int(value_str, &size) != 0 || size <= 0) {
		fprintf(stderr, "ERROR: size should be > 0\n");
		return EINVAL;
	}

	if (ioctl(fd, INT_STACK_IOCTL_SET_SIZE, &size) < 0) {
		if (errno == EINVAL)
			fprintf(stderr, "ERROR: size should be > 0\n");
		else
			perror("ioctl");
		return errno;
	}

	return 0;
}

static int cmd_push(int fd, const char *value_str)
{
	int value;
	ssize_t written;

	if (parse_int(value_str, &value) != 0) {
		fprintf(stderr, "ERROR: invalid integer value\n");
		return EINVAL;
	}

	written = write(fd, &value, sizeof(value));
	if (written < 0) {
		if (errno == ERANGE)
			fprintf(stderr, "ERROR: stack is full\n");
		else
			perror("write");
		return errno;
	}
	if (written != (ssize_t)sizeof(value)) {
		fprintf(stderr, "ERROR: short write\n");
		return EIO;
	}

	return 0;
}

static int cmd_pop(int fd)
{
	int value = 0;
	ssize_t nread = read(fd, &value, sizeof(value));

	if (nread < 0) {
		perror("read");
		return errno;
	}
	if (nread == 0) {
		printf("NULL\n");
		return 0;
	}
	if (nread != (ssize_t)sizeof(value)) {
		fprintf(stderr, "ERROR: short read\n");
		return EIO;
	}

	printf("%d\n", value);
	return 0;
}

static int cmd_unwind(int fd)
{
	for (;;) {
		int value = 0;
		ssize_t nread = read(fd, &value, sizeof(value));

		if (nread < 0) {
			perror("read");
			return errno;
		}
		if (nread == 0)
			return 0;
		if (nread != (ssize_t)sizeof(value)) {
			fprintf(stderr, "ERROR: short read\n");
			return EIO;
		}
		printf("%d\n", value);
	}
}

int main(int argc, char **argv)
{
	int fd;
	int rc = 0;

	if (argc < 2) {
		print_usage(argv[0]);
		return EINVAL;
	}

	fd = open_stack_device();
	if (fd < 0)
		return errno;

	if (strcmp(argv[1], "set-size") == 0) {
		if (argc != 3) {
			print_usage(argv[0]);
			rc = EINVAL;
			goto out;
		}
		rc = cmd_set_size(fd, argv[2]);
	} else if (strcmp(argv[1], "push") == 0) {
		if (argc != 3) {
			print_usage(argv[0]);
			rc = EINVAL;
			goto out;
		}
		rc = cmd_push(fd, argv[2]);
	} else if (strcmp(argv[1], "pop") == 0) {
		if (argc != 2) {
			print_usage(argv[0]);
			rc = EINVAL;
			goto out;
		}
		rc = cmd_pop(fd);
	} else if (strcmp(argv[1], "unwind") == 0) {
		if (argc != 2) {
			print_usage(argv[0]);
			rc = EINVAL;
			goto out;
		}
		rc = cmd_unwind(fd);
	} else {
		print_usage(argv[0]);
		rc = EINVAL;
	}

out:
	close(fd);
	return rc;
}
