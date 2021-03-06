/* -*- c-basic-offset: 8; tab-width: 8; indent-tabs-mode: t -*- */
/*
 * Copyright (c) 2021 Ralf Horstmann <ralf@ackstorm.de>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/stat.h>

#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "serial.h"

#define KB 1024
#define MEMBUFSIZE (512 * KB)
#define SERIAL_SPEED 460800

static uint8_t mem_buf[MEMBUFSIZE];

struct memtype_entry {
	const char *name;
	const char type;
	const int size;
} memtype_table[] = {
	{ "2764",  '0',   8 * KB },
	{ "27128", '1',  16 * KB },
	{ "27256", '2',  32 * KB },
	{ "27512", '3',  64 * KB },
	{ "27010", '4', 128 * KB },
	{ "27020", '5', 256 * KB },
	{ "27040", '6', 512 * KB },
};

enum {
	DEVICE_DISABLE = 0,
	DEVICE_ENABLE  = 1
};

enum {
	CMD_DISABLE  = 0x01,
	CMD_ENABLE   = 0x02,
	CMD_IDENTIFY = 0x04,
	CMD_LIST     = 0x08,
	CMD_WRITE    = 0x10,
};

static int
read_file(const char *filename, unsigned int startaddr, uint8_t *mem, ssize_t memlen)
{
	FILE *f;
	struct stat st;
	off_t flen = 0;

	if ((f = fopen(filename, "rb")) == NULL) {
		fprintf(stderr, "error: failed to open %s: %s\n", filename, strerror(errno));
		return -1;
	}

	if (fstat(fileno(f), &st) != 0) {
		perror("fstat");
		fclose(f);
		return -1;
	}
	flen = st.st_size;

	if (flen + startaddr > memlen) {
		fprintf(stderr, "error: memory exceeded (0x%04x + 0x%04jx > 0x%04lx)\n",
		        startaddr, flen, memlen);
		fclose(f);
		return -1;
	}

	if (fread(mem + startaddr, sizeof(uint8_t), flen, f) == 0) {
		perror("fread");
		fclose(f);
		return -1;
	}

	fclose(f);
	return flen;
}

static int
buf_write(int fd, const uint8_t *buf, size_t len)
{
	ssize_t n;

	while (len > 0) {
		n = write(fd, buf, len);
		if (n == -1) {
			if (errno == EINTR)
				continue;
			return -1;
		}
		buf += n;
		len -= n;
	}
	return 0;
}

static int
buf_read(int fd, uint8_t *buf, size_t len, int timeout)
{
	size_t buflen = len;
	struct pollfd pfd[1];
	int nfds;
	ssize_t n;

	pfd[0].fd = fd;
	pfd[0].events = POLLIN;

	while (len > 0) {
		nfds = poll(pfd, 1, timeout);
		if (nfds <= 0)
			return 0;
		n = read(fd, buf, len);
		if (n <= 0)
			return 0;
		len -= n;
		buf += n;
	}

	return buflen;
}

static void
memlist()
{
	size_t i;
	printf("supported memory configurations:\n\n");
	printf("name    size\n");
	for (i = 0; i < (sizeof(memtype_table) / sizeof(memtype_table[0])); i++) {
		printf("%-5s   %3dK\n", memtype_table[i].name, memtype_table[i].size / 1024 );
	}
	printf("\n");
}

static int
device_identify(int fd, const char* device, int verbose)
{
	char req[16+1];
	char resp[16+1];

	snprintf(req, sizeof(req), "MI000000000000\r\n");
	if (verbose)
		printf("request:  %s", req);
	if (buf_write(fd, (uint8_t*)req, sizeof(req) - 1) < 0) {
		fprintf(stderr, "error: failed to write request");
		return -1;
	}
	if (buf_read(fd, (uint8_t*)resp, 16, 5000) != 16) {
		fprintf(stderr, "error: failed to read response\n");
		return -1;
	}
	resp[16] = '\0';
	if (verbose)
		printf("response: %s", resp);
	printf("Device:  %s\n", device);
	printf("Version: %c\n", resp[2]);
	printf("Memory:  %c\n", resp[3]);
	return 0;
}

static int
device_config(int fd, const char memtype, const int reset, const char enable, const int verbose)
{
	char req[16+1];
	char resp[16+1];
	char reset_polarity;
	int reset_time;

	reset_polarity = '0';
	reset_time = 0;
	if (reset < 0) {
		reset_polarity = 'N';
		reset_time = -reset;
	} else if (reset > 0) {
		reset_polarity = 'P';
		reset_time = reset;
	}

	snprintf(req, sizeof(req), "MC%c%c%03d%cN000FF\r\n",
	         memtype, reset_polarity, reset_time,
	         enable == DEVICE_ENABLE ? 'E': 'D');
	if (verbose)
		printf("config request:  %s", req);
	if (buf_write(fd, (uint8_t*)req, sizeof(req) - 1) < 0) {
		fprintf(stderr, "error: failed to write config request");
		return -1;
	}
	if (buf_read(fd, (uint8_t*)resp, 16, 5000) != 16) {
		fprintf(stderr, "error: reading config response\n");
		return -1;
	}
	resp[16] = '\0';
	if (verbose)
		printf("config response: %s", resp);
	if (memcmp(req, resp, 8) != 0) {
		fprintf(stderr, "error: config response mismatch\n");
		return -1;
	}
	return 0;
}

static int
device_data(int fd, int memsize, int verbose)
{
	char req[16+1];
	char resp[16+1];

	snprintf(req, sizeof(req), "MD%04d000000FF\r\n", memsize / 1024 % 1000);
	if (verbose)
		printf("data header:     %s", req);
	if (buf_write(fd, (uint8_t*)req, sizeof(req) - 1) < 0)  {
		fprintf(stderr, "error: writing header\n");
		return -1;
	}
	if (verbose)
		printf("data bytes:      %d\n", memsize);
	if (buf_write(fd, mem_buf, memsize) < 0) {
		fprintf(stderr, "error: writing data\n");
		return -1;
	}
	if (buf_read(fd, (uint8_t*)resp, 16, 15000) != 16) {
		fprintf(stderr, "error: failed to read data response\n");
		return -1;
	}
	resp[16] = '\0';
	if (verbose)
		printf("data response:   %s", resp);
	if (memcmp(req, resp, 8) != 0) {
		fprintf(stderr, "error: data response mismatch\n");
		return -1;
	}
	return 0;
}


static const struct memtype_entry*
memtype_by_name(const char *memtype)
{
	size_t i;

	for (i = 0; i < (sizeof(memtype_table) / sizeof(memtype_table[0])); i++) {
		if (strcmp(memtype, memtype_table[i].name) == 0) {
			return &memtype_table[i];
		}
	}
	return NULL;
}

static int
str_to_num(const char* str, const char* msg, int min, int max, int *num)
{
	long value;
	char *endptr;

	value = strtol(str, &endptr, 0);
	if (endptr == str || *endptr != '\0') {
		fprintf(stderr, "error: invalid %s\n", msg);
		return -1;
	}
	if (value < min || value > max) {
		if (min >= 0 && max >= 0 && (min > 255 || max > 255))
			fprintf(stderr, "error: %s needs to in the range 0x%04x - 0x%04x\n", msg, min, max);
		else
			fprintf(stderr, "error: %s needs to in the range %d - %d\n", msg, min, max);

		return -1;
	}
	*num = value;
	return 0;
}

static void
usage(void)
{
	fprintf(stderr,
	        "usage: memsimctl [-d device] [-s start] [-r reset] [-z memfill] -m memtype -w file\n"
	        "       memsimctl [-d device] -m memtype -D\n"
	        "       memsimctl [-d device] -m memtype -E\n"
	        "       memsimctl [-d device] -i\n"
	        "       memsimctl -L\n"
	        "       memsimctl -h\n"
	        "\n"
	        "  -d device     serial device\n"
	        "  -D            disable buffers\n"
	        "  -E            enable buffers\n"
	        "  -h            print help\n"
	        "  -i            identify device\n"
	        "  -L            list memory types\n"
	        "  -m memtype    select memory type\n"
	        "  -s start      start address where input file is loaded\n"
	        "  -r reset      reset in ms, < 0 negative, > 0 positve, = 0 off\n"
	        "  -v            verbose output\n"
	        "  -w file       write file to emulator\n"
	        "  -z memfill    fill value for unused memory\n\n");
}

int
main(int argc, char *argv[])
{
	const struct memtype_entry *memtype = NULL;
	int res;
	int fd;
	int ch;
	int filelen;
	/* command line options */
	int command = 0;
	int verbose = 0;
	int startaddr = 0;
	int memfill = 0;
	int reset = -200;
	char *device = NULL;
	char *filename = NULL;

	while ((ch = getopt(argc, argv, "d:DEhiLm:r:s:vw:z:")) != -1) {
		switch (ch) {
		case 'd':
			device = optarg;
			break;
		case 'D':
			command |= CMD_DISABLE;
			break;
		case 'E':
			command |= CMD_ENABLE;
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'i':
			command |= CMD_IDENTIFY;
			break;
		case 'L':
			command |= CMD_LIST;
			break;
		case 'm':
			memtype = memtype_by_name(optarg);
			if (!memtype) {
				fprintf(stderr, "error: unknown memory type \"%s\"\n", optarg);
				return EXIT_FAILURE;
			}
			break;
		case 's':
			if (str_to_num(optarg, "startaddr", 0, INT_MAX, &startaddr) != 0)
				return EXIT_FAILURE;
			break;
		case 'r':
			if (str_to_num(optarg, "reset", -255, 255, &reset) != 0)
				return EXIT_FAILURE;
			break;
		case 'v':
			verbose++;
			break;
		case 'w':
			command |= CMD_WRITE;
			filename = optarg;
			break;
		case 'z':
			if (str_to_num(optarg, "memfill", 0, 255, &memfill) != 0)
				return EXIT_FAILURE;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc > 0) {
		fprintf(stderr, "error: superfluous positional argument: %s\n", argv[0]);
		return EXIT_FAILURE;
	}

	switch (command) {
	case CMD_LIST:
		memlist();
		return EXIT_SUCCESS;
	case CMD_IDENTIFY:
		fd = serial_open(device, SERIAL_SPEED);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_identify(fd, serial_device(device), verbose);
		close(fd);
		return res == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	case CMD_DISABLE:
		if (!memtype) {
			fprintf(stderr, "error: memtype required for buffer disable\n");
			return EXIT_FAILURE;
		}

		fd = serial_open(device, SERIAL_SPEED);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_config(fd, memtype->type, 0, DEVICE_DISABLE, verbose);
		close(fd);
		return res == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	case CMD_ENABLE:
		if (!memtype) {
			fprintf(stderr, "error: memtype required for buffer enable\n");
			return EXIT_FAILURE;
		}

		fd = serial_open(device, SERIAL_SPEED);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_config(fd, memtype->type, 0, DEVICE_ENABLE, verbose);
		close(fd);
		return res == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	case CMD_WRITE:
		if (!memtype) {
			fprintf(stderr, "error: memtype required for write\n");
			return EXIT_FAILURE;
		}
		memset(mem_buf, memfill, memtype->size);

		if ((filelen = read_file(filename, startaddr, mem_buf, memtype->size)) < 0) {
			return EXIT_FAILURE;
		}
		printf("\n");
		printf("[0x%05x : 0x%05x] (0x%05x) %s\n",
		       startaddr, startaddr + filelen - 1, filelen, filename);
		printf("[0x%05x : 0x%05x] (0x%05x) EPROM %s 0x%02x ",
		       0, memtype->size -1, memtype->size, memtype->name, memfill);
		if (reset == 0) {
			printf("noreset\n");
		} else {
			printf("%dms\n", reset);
		}
		printf("\n");

		fd = serial_open(device, SERIAL_SPEED);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_config(fd, memtype->type, reset, DEVICE_DISABLE, verbose);
		res += device_data(fd, memtype->size, verbose);
		close(fd);
		if (res == 0)
			printf("Transfer: OK\n\n");
		else
			printf("Transfer: FAILED\n\n");
		return res == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	fprintf(stderr, "error: use exactly one of: [-i] [-D] [-E] [-L] [-w filename]\n");
	return EXIT_FAILURE;
}
