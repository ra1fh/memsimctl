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

#include <sys/ioctl.h>
#include <fcntl.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "serial.h"

#define SERIAL_DEFAULT "/dev/cuaU0"

static int
serial_setup(int fd, int speed)
{
	struct termios tio;

	if (tcgetattr(fd, &tio) < 0) {
		perror("tcgetattr");
		return -1;
	}
	cfmakeraw(&tio);
	tio.c_iflag |= (IGNBRK|IGNPAR);
	cfsetspeed(&tio, speed);
	if (tcsetattr(fd, TCSAFLUSH, &tio) < 0) {
		perror("tcsetattr");
		return -1;
	}
	return 0;
}

const char*
serial_device(const char *device)
{
	if (device)
		return device;
	else
		return SERIAL_DEFAULT;
}

int
serial_open(const char *device, int speed)
{
	int fd;

	if (!device) {
		device = SERIAL_DEFAULT;
	}

	fd = open(device, O_RDWR);
	if (fd < 0) {
		perror("open");
		return -1;
	}
	if (serial_setup(fd, speed) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}
