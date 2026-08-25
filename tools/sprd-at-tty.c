// SPDX-License-Identifier: GPL-2.0
/*
 * Small, script-friendly AT client for sprd_vcom devices.
 *
 * Copyright (C) 2026 m.fahim <fahimohy@gmail.com>
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include <errno.h>
#include <fcntl.h>
#include <glob.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#ifndef LEGACY_DEVICE
#define LEGACY_DEVICE "/dev/sprd-at"
#endif
#ifndef DEVICE_GLOB
#define DEVICE_GLOB "/dev/sprd/at-*"
#endif
#define DEFAULT_TIMEOUT_MS 3000
#define MAX_TIMEOUT_MS 3600000
#define MAX_COMMAND_BYTES 16384
#define MAX_RESPONSE_BYTES 65536

enum response_state {
	RESPONSE_PENDING,
	RESPONSE_OK,
	RESPONSE_ERROR
};

static int64_t monotonic_ms(void)
{
	struct timespec now;

	if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
		return -1;

	return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static int contains(const char *buf, size_t len, const char *mark)
{
	size_t mark_len = strlen(mark);
	size_t i;

	if (mark_len > len)
		return 0;

	for (i = 0; i + mark_len <= len; i++) {
		if (memcmp(buf + i, mark, mark_len) == 0)
			return 1;
	}

	return 0;
}

static enum response_state response_state(const char *buf, size_t len)
{
	static const char * const errors[] = {
		"\r\nERROR\r\n",
		"\r\n+CME ERROR:",
		"\r\n+CMS ERROR:",
		"\r\nNO CARRIER\r\n",
		"\r\nNO ANSWER\r\n",
		"\r\nBUSY\r\n"
	};
	size_t i;

	for (i = 0; i < sizeof(errors) / sizeof(errors[0]); i++) {
		size_t start_len = strlen(errors[i]) - 2;

		if (contains(buf, len, errors[i]) ||
		    (len >= start_len &&
		     memcmp(buf, errors[i] + 2, start_len) == 0))
			return RESPONSE_ERROR;
	}

	if (contains(buf, len, "\r\nOK\r\n") ||
	    (len >= 4 && memcmp(buf, "OK\r\n", 4) == 0))
		return RESPONSE_OK;

	return RESPONSE_PENDING;
}

static int configure_tty(int fd)
{
	struct termios tio;

	if (tcgetattr(fd, &tio) < 0)
		return -1;

	cfmakeraw(&tio);
	tio.c_cflag |= CLOCAL | CREAD;
	tio.c_cflag &= ~CRTSCTS;
	cfsetispeed(&tio, B115200);
	cfsetospeed(&tio, B115200);
	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;

	return tcsetattr(fd, TCSANOW, &tio);
}

static int write_all(int fd, const char *buf, size_t len)
{
	size_t done = 0;

	while (done < len) {
		ssize_t written = write(fd, buf + done, len - done);

		if (written > 0) {
			done += (size_t)written;
			continue;
		}

		if (written < 0 && errno == EINTR)
			continue;

		if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
			struct pollfd pfd = {
				.fd = fd,
				.events = POLLOUT
			};

			if (poll(&pfd, 1, DEFAULT_TIMEOUT_MS) > 0)
				continue;
		}

		if (written == 0)
			errno = EIO;
		return -1;
	}

	return 0;
}

static int read_response(int fd, const char *command, int timeout_ms)
{
	char response[MAX_RESPONSE_BYTES];
	enum response_state state = RESPONSE_PENDING;
	int64_t deadline;
	size_t used = 0;

	deadline = monotonic_ms();
	if (deadline < 0) {
		perror("clock_gettime");
		return -1;
	}
	deadline += timeout_ms;

	while (state == RESPONSE_PENDING) {
		struct pollfd pfd = {
			.fd = fd,
			.events = POLLIN
		};
		int64_t now = monotonic_ms();
		int remaining;
		int ret;

		if (now < 0) {
			perror("clock_gettime");
			return -1;
		}
		if (now >= deadline)
			break;

		remaining = (int)(deadline - now);
		ret = poll(&pfd, 1, remaining);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			return -1;
		}
		if (ret == 0)
			break;

		if (pfd.revents & POLLIN) {
			ssize_t count;

			if (used == sizeof(response)) {
				fprintf(stderr, "response exceeds %u bytes\n",
					(unsigned int)sizeof(response));
				return -1;
			}

			count = read(fd, response + used, sizeof(response) - used);
			if (count < 0) {
				if (errno == EINTR || errno == EAGAIN)
					continue;
				perror("read");
				return -1;
			}
			if (count == 0) {
				fprintf(stderr, "device disconnected while reading\n");
				return -1;
			}

			used += (size_t)count;
			state = response_state(response, used);
			if (used == sizeof(response) && state == RESPONSE_PENDING) {
				fprintf(stderr, "response exceeds %u bytes\n",
					(unsigned int)sizeof(response));
				return -1;
			}
		}

		if (state == RESPONSE_PENDING &&
		    (pfd.revents & (POLLERR | POLLHUP | POLLNVAL))) {
			fprintf(stderr, "tty disconnected (poll events 0x%x)\n",
				pfd.revents);
			return -1;
		}
	}

	if (used) {
		if (fwrite(response, 1, used, stdout) != used) {
			perror("stdout");
			return -1;
		}
		if (response[used - 1] != '\n')
			putchar('\n');
		fflush(stdout);
	}

	if (state == RESPONSE_OK)
		return 0;
	if (state == RESPONSE_ERROR) {
		fprintf(stderr, "modem rejected command: %s\n", command);
		return -1;
	}

	fprintf(stderr, "timeout waiting for final response to: %s\n", command);
	return -1;
}

static int send_command(int fd, const char *command, int timeout_ms)
{
	char *wire_command;
	size_t len = strlen(command);
	int ret;

	while (len && (command[len - 1] == '\r' || command[len - 1] == '\n'))
		len--;

	if (!len || len > MAX_COMMAND_BYTES) {
		fprintf(stderr, "command length must be between 1 and %u bytes\n",
			MAX_COMMAND_BYTES);
		return -1;
	}

	wire_command = malloc(len + 2);
	if (!wire_command) {
		perror("malloc");
		return -1;
	}

	memcpy(wire_command, command, len);
	wire_command[len] = '\r';
	wire_command[len + 1] = '\n';

	if (tcflush(fd, TCIFLUSH) < 0) {
		perror("tcflush");
		free(wire_command);
		return -1;
	}

	ret = write_all(fd, wire_command, len + 2);
	free(wire_command);
	if (ret < 0) {
		perror("write");
		return -1;
	}

	if (tcdrain(fd) < 0) {
		perror("tcdrain");
		return -1;
	}

	return read_response(fd, command, timeout_ms);
}

static int find_devices(glob_t *devices)
{
	int ret;

	memset(devices, 0, sizeof(*devices));
	ret = glob(DEVICE_GLOB, GLOB_NOSORT, NULL, devices);
	if (ret == GLOB_NOMATCH)
		return 0;
	if (ret != 0) {
		fprintf(stderr, "failed to enumerate %s\n", DEVICE_GLOB);
		return -1;
	}

	return 0;
}

static void print_devices(void)
{
	glob_t devices;
	size_t i;

	if (find_devices(&devices) < 0)
		return;

	for (i = 0; i < devices.gl_pathc; i++)
		puts(devices.gl_pathv[i]);

	if (devices.gl_pathc == 0 && access(LEGACY_DEVICE, F_OK) == 0)
		puts(LEGACY_DEVICE);

	globfree(&devices);
}

static char *select_device(void)
{
	glob_t devices;
	char *selected = NULL;
	size_t i;

	if (find_devices(&devices) < 0)
		return NULL;

	if (devices.gl_pathc == 1) {
		errno = 0;
		selected = strdup(devices.gl_pathv[0]);
	} else if (devices.gl_pathc > 1) {
		fprintf(stderr, "multiple SPRD AT ports found; use -d DEVICE:\n");
		for (i = 0; i < devices.gl_pathc; i++)
			fprintf(stderr, "  %s\n", devices.gl_pathv[i]);
	} else if (access(LEGACY_DEVICE, F_OK) == 0) {
		errno = 0;
		selected = strdup(LEGACY_DEVICE);
	} else {
		fprintf(stderr,
			"no SPRD AT port found; reconnect the modem or use -d DEVICE\n");
	}

	if (!selected && devices.gl_pathc <= 1 && errno == ENOMEM)
		perror("strdup");

	globfree(&devices);
	return selected;
}

static int parse_timeout(const char *text, int *timeout_ms)
{
	char *end;
	long value;

	errno = 0;
	value = strtol(text, &end, 10);
	if (errno || !*text || *end || value < 1 || value > MAX_TIMEOUT_MS)
		return -1;

	*timeout_ms = (int)value;
	return 0;
}

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s [-d DEVICE] [-t TIMEOUT_MS] COMMAND [COMMAND ...]\n"
		"       %s -l\n"
		"Without -d, exactly one /dev/sprd/at-* device is selected.\n",
		program, program);
}

#ifndef SPRD_AT_TTY_TEST
int main(int argc, char **argv)
{
	const char *device_arg = NULL;
	char *auto_device = NULL;
	int timeout_ms = DEFAULT_TIMEOUT_MS;
	int list_only = 0;
	int opt;
	int fd;
	int rc = 0;
	int i;

	while ((opt = getopt(argc, argv, "d:t:lh")) != -1) {
		switch (opt) {
		case 'd':
			device_arg = optarg;
			break;
		case 't':
			if (parse_timeout(optarg, &timeout_ms) < 0) {
				fprintf(stderr, "invalid timeout: %s\n", optarg);
				return 2;
			}
			break;
		case 'l':
			list_only = 1;
			break;
		case 'h':
			usage(argv[0]);
			return 0;
		default:
			usage(argv[0]);
			return 2;
		}
	}

	if (list_only) {
		if (device_arg || optind != argc) {
			usage(argv[0]);
			return 2;
		}
		print_devices();
		return 0;
	}

	if (optind >= argc) {
		usage(argv[0]);
		return 2;
	}

	if (!device_arg) {
		auto_device = select_device();
		if (!auto_device)
			return 1;
		device_arg = auto_device;
	}

	fd = open(device_arg, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror(device_arg);
		free(auto_device);
		return 1;
	}

	if (configure_tty(fd) < 0) {
		perror("tcsetattr");
		close(fd);
		free(auto_device);
		return 1;
	}

	for (i = optind; i < argc; i++) {
		if (argc - optind > 1)
			printf(">>> %s\n", argv[i]);
		if (send_command(fd, argv[i], timeout_ms) < 0)
			rc = 1;
	}

	if (close(fd) < 0) {
		perror("close");
		rc = 1;
	}
	free(auto_device);

	return rc;
}
#endif
