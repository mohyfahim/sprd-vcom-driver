/*
 * sprd-at-tty.c
 *
 * Small userspace client for /dev/sprd-at exposed by sprd_vcom.ko.
 * Suitable for one-shot or periodic AT queries.
 *
 * Build:
 *   gcc -O2 -Wall -Wextra sprd-at-tty.c -o sprd-at-tty
 *
 * Examples:
 *   ./sprd-at-tty AT
 *   ./sprd-at-tty 'AT+CEREG=2' 'AT+CEREG?'
 *   ./sprd-at-tty -d /dev/sprd-at 'AT+CSQ'
 */

#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#define DEFAULT_DEVICE "/dev/sprd-at"
#define DEFAULT_TIMEOUT_MS 3000

static int terminal_done(const char *buf, size_t len)
{
	static const char *marks[] = {
		"\r\nOK\r\n",
		"\r\nERROR\r\n",
		"+CME ERROR:",
		"+CMS ERROR:",
		"NO CARRIER\r\n"
	};

	for (size_t m = 0; m < sizeof(marks) / sizeof(marks[0]); ++m) {
		size_t ml = strlen(marks[m]);

		if (len < ml)
			continue;

		for (size_t i = 0; i + ml <= len; ++i)
			if (!memcmp(buf + i, marks[m], ml))
				return 1;
	}

	return 0;
}

static int configure_tty(int fd)
{
	struct termios tio;

	if (tcgetattr(fd, &tio) < 0)
		return -1;

	cfmakeraw(&tio);
	tio.c_cflag |= CLOCAL | CREAD;
	tio.c_cflag &= ~CRTSCTS;

	/*
	 * USB VCOM does not have a meaningful UART baud rate, but setting a
	 * conventional value keeps terminal programs happy.
	 */
	cfsetispeed(&tio, B115200);
	cfsetospeed(&tio, B115200);

	tio.c_cc[VMIN] = 0;
	tio.c_cc[VTIME] = 0;

	return tcsetattr(fd, TCSANOW, &tio);
}

static int send_command(int fd, const char *command, int timeout_ms)
{
	char out[4096];
	char in[65536];
	size_t used = 0;
	size_t n = strlen(command);

	while (n && (command[n - 1] == '\r' || command[n - 1] == '\n'))
		--n;

	if (n + 2 >= sizeof(out)) {
		fprintf(stderr, "command too long\n");
		return -1;
	}

	memcpy(out, command, n);
	out[n++] = '\r';
	out[n++] = '\n';

	tcflush(fd, TCIFLUSH);

	ssize_t wr = write(fd, out, n);
	if (wr < 0) {
		perror("write");
		return -1;
	}
	if ((size_t)wr != n) {
		fprintf(stderr, "short write: %zd/%zu\n", wr, n);
		return -1;
	}

	if (tcdrain(fd) < 0) {
		perror("tcdrain");
		return -1;
	}

	int remaining = timeout_ms;

	while (remaining > 0 && used < sizeof(in)) {
		int slice = remaining > 250 ? 250 : remaining;
		struct pollfd pfd = {
			.fd = fd,
			.events = POLLIN
		};

		int r = poll(&pfd, 1, slice);
		remaining -= slice;

		if (r < 0) {
			if (errno == EINTR)
				continue;
			perror("poll");
			return -1;
		}

		if (r == 0)
			continue;

		if (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
			fprintf(stderr, "tty poll error: revents=0x%x\n",
				pfd.revents);
			return -1;
		}

		if (pfd.revents & POLLIN) {
			ssize_t rd = read(fd, in + used, sizeof(in) - used);

			if (rd < 0) {
				if (errno == EAGAIN || errno == EINTR)
					continue;
				perror("read");
				return -1;
			}

			if (rd == 0)
				continue;

			used += (size_t)rd;

			if (terminal_done(in, used))
				break;
		}
	}

	if (used) {
		fwrite(in, 1, used, stdout);
		if (in[used - 1] != '\n')
			putchar('\n');
		fflush(stdout);
		return 0;
	}

	fprintf(stderr, "timeout waiting for response to: %s\n", command);
	return -1;
}

static void usage(const char *p)
{
	fprintf(stderr,
		"Usage: %s [-d DEVICE] [-t TIMEOUT_MS] COMMAND [COMMAND ...]\n"
		"Default DEVICE: %s\n",
		p, DEFAULT_DEVICE);
}

int main(int argc, char **argv)
{
	const char *device = DEFAULT_DEVICE;
	int timeout_ms = DEFAULT_TIMEOUT_MS;
	int opt;

	while ((opt = getopt(argc, argv, "d:t:h")) != -1) {
		switch (opt) {
		case 'd':
			device = optarg;
			break;
		case 't':
			timeout_ms = atoi(optarg);
			break;
		default:
			usage(argv[0]);
			return opt == 'h' ? 0 : 2;
		}
	}

	if (optind >= argc) {
		usage(argv[0]);
		return 2;
	}

	int fd = open(device, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror(device);
		return 1;
	}

	if (configure_tty(fd) < 0) {
		perror("tcsetattr");
		close(fd);
		return 1;
	}

	int rc = 0;

	for (int i = optind; i < argc; ++i) {
		if (argc - optind > 1)
			printf(">>> %s\n", argv[i]);

		if (send_command(fd, argv[i], timeout_ms) < 0)
			rc = 1;
	}

	close(fd);
	return rc;
}
