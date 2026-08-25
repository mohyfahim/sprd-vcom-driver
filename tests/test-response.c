// SPDX-License-Identifier: GPL-2.0
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <sys/socket.h>

#define SPRD_AT_TTY_TEST
#include "../tools/sprd-at-tty.c"

struct reader_args {
	int fd;
	size_t expected;
	size_t received;
};

static void *drain_socket(void *opaque)
{
	struct reader_args *args = opaque;
	char buffer[4096];
	struct timespec delay = {
		.tv_nsec = 20000000
	};

	nanosleep(&delay, NULL);
	while (args->received < args->expected) {
		ssize_t count = read(args->fd, buffer, sizeof(buffer));

		assert(count > 0);
		args->received += (size_t)count;
	}

	return NULL;
}

static void test_partial_write(void)
{
	struct reader_args args = { 0 };
	char *payload;
	pthread_t reader;
	int sockets[2];
	int flags;

	assert(socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
	flags = fcntl(sockets[0], F_GETFL);
	assert(flags >= 0);
	assert(fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == 0);

	args.fd = sockets[1];
	args.expected = 1024 * 1024;
	payload = malloc(args.expected);
	assert(payload);
	memset(payload, 'A', args.expected);

	assert(pthread_create(&reader, NULL, drain_socket, &args) == 0);
	assert(write_all(sockets[0], payload, args.expected) == 0);
	assert(pthread_join(reader, NULL) == 0);
	assert(args.received == args.expected);

	free(payload);
	close(sockets[0]);
	close(sockets[1]);
}

int main(void)
{
	int timeout = 0;

	assert(response_state("\r\nOK\r\n", 6) == RESPONSE_OK);
	assert(response_state("OK\r\n", 4) == RESPONSE_OK);
	assert(response_state("\r\nERROR\r\n", 9) == RESPONSE_ERROR);
	assert(response_state("ERROR\r\n", 7) == RESPONSE_ERROR);
	assert(response_state("\r\n+CME ERROR: 10\r\n", 18) ==
	       RESPONSE_ERROR);
	assert(response_state("\r\n+CEREG: 1\r\n", 13) ==
	       RESPONSE_PENDING);
	assert(response_state("\r\nO", 3) == RESPONSE_PENDING);

	assert(parse_timeout("1", &timeout) == 0 && timeout == 1);
	assert(parse_timeout("3600000", &timeout) == 0 &&
	       timeout == 3600000);
	assert(parse_timeout("0", &timeout) < 0);
	assert(parse_timeout("-1", &timeout) < 0);
	assert(parse_timeout("100ms", &timeout) < 0);
	assert(parse_timeout("", &timeout) < 0);
	assert(monotonic_ms() > 0);
	test_partial_write();

	return 0;
}
