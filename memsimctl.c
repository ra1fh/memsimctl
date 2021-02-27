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
	int device_selftest;
	char memtype;
	int memsize;
};

static int
read_file(const char *filename, off_t offset, uint8_t *mem, int memlen)
{
	FILE *f;
	off_t flen = 0;

	if ((f = fopen(filename, "rb")) == NULL) {
		fprintf(stderr, "error: failed to open %s: %s\n", filename, strerror(errno));
		return -1;
	}

	if (fseeko(f, 0, SEEK_END) != 0) {
		perror("fseeko end");
		fclose(f);
		return -1;
	}

	if ((flen = ftello(f)) < 0) {
		perror("ftello end");
		fclose(f);
		return -1;
	}

	if (flen > memlen) {
		fprintf(stderr, "error: file too large\n");
		fclose(f);
		return -1;
	}

	if (offset > 0) {
		if (fseeko(f, offset, SEEK_SET) != 0) {
			perror("fseeko offset");
			fclose(f);
			return -1;
		}
	} else if (offset == 0) {
		if (fseeko(f, 0, SEEK_SET) != 0) {
			perror("fseeko start");
			fclose(f);
			return -1;
		}
	} else {
		fprintf(stderr, "error: negative offset\n");
		fclose(f);
		return -1;
	}

	if (fread(mem, sizeof(uint8_t), memlen, f) == 0) {
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
	printf("name    size\n");
	for (i = 0; i < (sizeof(memtype_table) / sizeof(memtype_table[0])); i++) {
		printf("%-5s   %3dK\n", memtype_table[i].name, memtype_table[i].size / 1024 );
	}
}

static int
device_info(int fd)
{
	char req[16+1];
	char resp[16+1];

	snprintf(req, sizeof(req), "MI000000000000\r\n");
	printf("request:  %s", req);
	if (buf_write(fd, (uint8_t*)req, sizeof(req) - 1) < 0) {
		fprintf(stderr, "error: failed to write request");
		return EXIT_FAILURE;
	}
	if (buf_read(fd, (uint8_t*)resp, 16, 5000) != 16) {
		fprintf(stderr, "error: failed to read response\n");
		return EXIT_FAILURE;
	}
	resp[16] = '\0';
	printf("response: %s", resp);
	return EXIT_SUCCESS;
}

static int
device_program(int fd, const struct device_config* conf)
{
	char req[16+1];
	char resp[16+1];

	snprintf(req, sizeof(req), "MC%c%c%03d%c%c00023\r\n",
	         conf->memtype, conf->reset_polarity, conf->reset_time,
		 conf->device_enable, conf->device_selftest);
	printf("config request:  %s", req);
	if (buf_write(fd, (uint8_t*)req, sizeof(req) - 1) < 0) {
		fprintf(stderr, "error: failed to write config request");
		return EXIT_FAILURE;
	}
	if (buf_read(fd, (uint8_t*)resp, 16, 5000) != 16) {
		fprintf(stderr, "error: reading config response\n");
		return EXIT_FAILURE;
	}
	resp[16] = '\0';
	printf("config response: %s", resp);
	if (memcmp(req, resp, 8) != 0) {
		fprintf(stderr, "error: config response mismatch\n");
		return EXIT_FAILURE;
	}

	snprintf(req, sizeof(req), "MD%04d00000058\r\n", conf->memsize / 1024 % 1000);
	printf("data header: %s", req);
	if (buf_write(fd, (uint8_t*)req, sizeof(req) - 1) < 0)  {
		fprintf(stderr, "error: writing header\n");
		return EXIT_FAILURE;
	}
	printf("data bytes: %d\n", conf->memsize);
	if (buf_write(fd, mem_buf, conf->memsize) < 0) {
		fprintf(stderr, "error: writing data\n");
		return EXIT_FAILURE;
	}
	if (buf_read(fd, (uint8_t*)resp, 16, 15000) != 16) {
		fprintf(stderr, "error: failed to read data response\n");
		return EXIT_FAILURE;
	}
	resp[16] = '\0';
	printf("data response: %s", resp);
	if (memcmp(req, resp, 8) != 0) {
		fprintf(stderr, "error: data response mismatch\n");
		return EXIT_FAILURE;
	}
	printf("transfer complete\n");
	return EXIT_SUCCESS;
}

const struct memtype_entry*
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

const struct memtype_entry*
memtype_by_size(int size)
{
	size_t i;

	for (i = 0; i < (sizeof(memtype_table) / sizeof(memtype_table[0])); i++) {
		if (memtype_table[i].size == size) {
			return &memtype_table[i];
		}
	}
	return NULL;
}

const struct memtype_entry*
memtype_by_min_size(int size)
{
	size_t i;

	for (i = 0; i < (sizeof(memtype_table) / sizeof(memtype_table[0])); i++) {
		if (memtype_table[i].size >= size) {
			return &memtype_table[i];
		}
	}
	return NULL;
}

static void
usage(void)
{
	fprintf(stderr, "usage: memsimctl [-ehilp] [-d device] [-m memtype] [-o offset] [-r reset]\n");
	fprintf(stderr, "                 [-w file]\n");
	fprintf(stderr, "\n");
	fprintf(stderr, "  -d device   serial device\n");
	fprintf(stderr, "  -e          enable emulation\n");
	fprintf(stderr, "  -h          print help\n");
	fprintf(stderr, "  -i          identify device\n");
	fprintf(stderr, "  -l          list memory types\n");
	fprintf(stderr, "  -m memtype  select memory type\n");
	fprintf(stderr, "  -o offset   specify input file offset\n");
	fprintf(stderr, "  -p          reset pulse positive, default: negative\n");
	fprintf(stderr, "  -r reset    reset pulse duration in ms, default: off\n");
	fprintf(stderr, "  -w file     write file to emulator\n\n");
}

int
main(int argc, char *argv[])
{
	const struct memtype_entry *memtype_entry_ptr = NULL;
	int res;
	int fd;
	int ch;
	long value;
	char *endptr;
	int filelen;
	off_t fileoff = 0;
	struct device_config conf = {
		.reset_time     =  0,
		.reset_polarity = '0',
		.device_enable  = 'D',
		.device_selftest= 'N'
		/* based on input: */
		/* .memtype = ...  */
		/* .memsize = ...  */
	};
	/* command line options */
	char *device = NULL;
	int getinfo = 0;
	int printlist = 0;
	int positive = 0;
	char *memtype = NULL;
	char *offset = NULL;
	char *reset = NULL;
	char *filename = NULL;

	while ((ch = getopt(argc, argv, "d:ehilm:o:pr:w:")) != -1) {
		switch (ch) {
		case 'd':
			device = optarg;
			break;
		case 'e':
			conf.device_enable = 'E';
			break;
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'i':
			getinfo = 1;
			break;
		case 'l':
			printlist = 1;
			break;
		case 'm':
			memtype = optarg;
			break;
		case 'o':
			offset = optarg;
			break;
		case 'p':
			positive = 1;
			break;
		case 'r':
			reset = optarg;
			break;
		case 'w':
			filename = optarg;
			break;
		case '?':
			return EXIT_FAILURE;
		}
	}

	argc -= optind;
	argv += optind;

	if (printlist) {
		memlist();
		return 0;
	}

	if (memtype) {
		memtype_entry_ptr = memtype_by_name(memtype);
		if (!memtype_entry_ptr) {
			fprintf(stderr, "error: unknown memory type \"%s\"\n", memtype);
			return EXIT_FAILURE;
		}
	}

	if (offset) {
		fileoff = strtol(offset, &endptr, 0);
		if (endptr == offset || *endptr != '\0') {
			fprintf(stderr, "error: invalid offset\n");
			return EXIT_FAILURE;
		}
	}

	if (reset) {
		value = strtol(reset, &endptr, 0);
		if (endptr == reset || *endptr != '\0') {
			fprintf(stderr, "error: invalid reset time\n");
			return EXIT_FAILURE;
		}
		if (value == 0) {
			/* keep the default */
		} else if (value > 0 && value <= 255) {
			conf.reset_polarity = positive ? 'P' : 'N';
			conf.reset_time = value;
		} else {
			fprintf(stderr, "error: reset time out of range\n");
			return EXIT_FAILURE;
		}
	}

	if (getinfo) {
		fd = serial_open(device);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_info(fd);
		close(fd);
		return res;
	}

	if (filename) {
		filelen = read_file(filename, fileoff, mem_buf, sizeof(mem_buf));
		if (filelen < 0) {
			return EXIT_FAILURE;
		}
		if (memtype_entry_ptr && filelen > memtype_entry_ptr->size) {
			fprintf(stderr, "error: data exceeds selected memory type\n");
			return EXIT_FAILURE;
		}
		if ((memtype_entry_ptr = memtype_by_size(filelen)) == NULL) {
			if ((memtype_entry_ptr = memtype_by_min_size(filelen)) == NULL) {
				fprintf(stderr, "error: no suitable memory type found\n");
			}
		}
		if (!memtype_entry_ptr) {
			fprintf(stderr, "error: no memory config found\n");
			return EXIT_FAILURE;
		}
		conf.memtype = memtype_entry_ptr->type;
		conf.memsize = memtype_entry_ptr->size;

		printf("file size: %d\n", filelen);
		printf("simulated size: %d\n", conf.memsize);
		printf("selected config: %c\n", conf.memtype);

		fd = serial_open(device);
		if (fd < 0)
			return EXIT_FAILURE;
		res = device_program(fd, &conf);
		close(fd);
		return res;
	}

	return EXIT_SUCCESS;
}
