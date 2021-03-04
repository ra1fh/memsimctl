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
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "serial.h"

#define KB 1024
#define MEMBUFSIZE (512 * KB)

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

struct device_config {
	short int reset_time;
	char reset_polarity;
	int device_enable;
	char memtype;
	int memsize;
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
device_info(int fd, const char* device, int verbose)
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
device_config(int fd, const struct device_config* conf, int verbose)
{
	char req[16+1];
	char resp[16+1];

	snprintf(req, sizeof(req), "MC%c%c%03d%cN000FF\r\n",
	         conf->memtype, conf->reset_polarity, conf->reset_time,
	         conf->device_enable);
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
device_data(int fd, const struct device_config* conf, int verbose)
{
	char req[16+1];
	char resp[16+1];

	snprintf(req, sizeof(req), "MD%04d000000FF\r\n", conf->memsize / 1024 % 1000);
	if (verbose)
		printf("data header:     %s", req);
	if (buf_write(fd, (uint8_t*)req, sizeof(req) - 1) < 0)  {
		fprintf(stderr, "error: writing header\n");
		return -1;
	}
	if (verbose)
		printf("data bytes:      %d\n", conf->memsize);
	if (buf_write(fd, mem_buf, conf->memsize) < 0) {
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
	        "usage: memsimctl [-d device] [-s start] [-r reset] [-z memfill] -m memtype -w file\n");
	fprintf(stderr, "       memsimctl [-d device] -m memtype -D\n");
	fprintf(stderr, "       memsimctl [-d device] -i\n");
	fprintf(stderr, "       memsimctl -h\n");
	fprintf(stderr, "       memsimctl -L\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -d device     serial device\n");
	fprintf(stderr, "  -D            disable buffers\n");
	fprintf(stderr, "  -h            print help\n");
	fprintf(stderr, "  -i            identify device\n");
	fprintf(stderr, "  -L            list memory types\n");
	fprintf(stderr, "  -m memtype    select memory type\n");
	fprintf(stderr, "  -s start      start address where input file is loaded\n");
	fprintf(stderr, "  -r reset      reset in ms, < 0 negative, > 0 positve, = 0 off\n");
	fprintf(stderr, "  -v            verbose output\n");
	fprintf(stderr, "  -w file       write file to emulator\n");
	fprintf(stderr, "  -z memfill    fill value for unused memory\n\n");
}

int
main(int argc, char *argv[])
{
	const struct memtype_entry *memtype_ptr = NULL;
	int res;
	int fd;
	int ch;
	int filelen;
	int resetval = -200;
	int memfillval = 0;
	int startaddrval = 0;
	int verbose = 0;
	struct device_config conf;
	/* command line options */
	int getinfo = 0;
	int printlist = 0;
	int disable = 0;
	char *device = NULL;
	char *memtype = NULL;
	char *reset = NULL;
	char *startaddr = NULL;
	char *filename = NULL;
	char *memfill = NULL;

	while ((ch = getopt(argc, argv, "d:DhiLm:r:s:vw:z:")) != -1) {
		switch (ch) {
		case 'd':
			device = optarg;
			break;
		case 'D':
			disable = 1;
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'i':
			getinfo = 1;
			break;
		case 'L':
			printlist = 1;
			break;
		case 'm':
			memtype = optarg;
			break;
		case 's':
			startaddr = optarg;
			break;
		case 'r':
			reset = optarg;
			break;
		case 'v':
			verbose++;
			break;
		case 'w':
			filename = optarg;
			break;
		case 'z':
			memfill = optarg;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (printlist) {
		memlist();
		return EXIT_SUCCESS;
	}

	if (getinfo) {
		fd = serial_open(device);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_info(fd, serial_device(device), verbose);
		close(fd);
		return res == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (!memtype) {
		fprintf(stderr, "error: memtype required for write mode and buffer disable\n");
		return EXIT_FAILURE;
	}

	memtype_ptr = memtype_by_name(memtype);
	if (!memtype_ptr) {
		fprintf(stderr, "error: unknown memory type \"%s\"\n", memtype);
		return EXIT_FAILURE;
	}
	conf.memtype = memtype_ptr->type;
	conf.memsize = memtype_ptr->size;

	if (disable) {
		fd = serial_open(device);
		if (fd < 0)
			return EXIT_FAILURE;
		conf.reset_time = 0;
		conf.reset_polarity = '0';
		conf.device_enable = 'D';
		res = device_config(fd, &conf, verbose);
		close(fd);
		return res == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (!filename) {
		fprintf(stderr, "error: no operation specified\n");
		return EXIT_FAILURE;
	}

	if (startaddr) {
		if (str_to_num(startaddr, "start", 0, memtype_ptr->size - 1, &startaddrval) != 0)
			return EXIT_FAILURE;
	}

	if (memfill) {
		if (str_to_num(memfill, "memfill", 0, 255, &memfillval) != 0) {
			return EXIT_FAILURE;
		}
		memset(mem_buf, memfillval, memtype_ptr->size);
	}

	if (reset) {
		if (str_to_num(reset, "reset", -255, 255, &resetval) != 0)
			return EXIT_FAILURE;
	}

	if (resetval == 0) {
		conf.reset_polarity = '0';
		conf.reset_time = 0;
	} else if (resetval < 0) {
		conf.reset_polarity = 'N';
		conf.reset_time = -resetval;
	} else if (resetval > 0) {
		conf.reset_polarity = 'P';
		conf.reset_time = resetval;
	}
	conf.device_enable = 'E';

	if (filename && memtype_ptr) {
		if ((filelen = read_file(filename, startaddrval, mem_buf, memtype_ptr->size)) < 0) {
			return EXIT_FAILURE;
		}
		printf("\n");
		printf("%s: [0x%06x : 0x%06x] (0x%04x)\n", filename, startaddrval, startaddrval + filelen - 1,
		       filelen);
		printf("\n");
		printf("EPROM:   %5s (0x%04x)\n", memtype_ptr->name, memtype_ptr->size);
		printf("Fill:     0x%02x\n", memfillval);
		if (conf.reset_time) {
			printf("Reset:    %4d ms\n", (conf.reset_polarity == 'N' ? -conf.reset_time : conf.reset_time));
		} else {
			printf("Reset:     OFF\n");
		}

		fd = serial_open(device);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_config(fd, &conf, verbose);
		res += device_data(fd, &conf, verbose);
		close(fd);
		if (res == 0)
			printf("Transfer:   OK (0x%04x)\n\n", conf.memsize);
		else
			printf("Transfer: FAILED\n\n");
		return res == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	/* not reached */
	return EXIT_FAILURE;
}
