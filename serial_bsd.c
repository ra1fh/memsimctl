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

#define SERIAL_SPEED 460800
#define SERIAL_DEFAULT "/dev/cuaU0"


static int
serial_setup(int fd)
{
	struct termios tio;

	if (tcgetattr(fd, &tio) < 0) {
		perror("tcgetattr");
		return -1;
	}
	tio.c_iflag &= ~(IGNBRK|BRKINT|IGNPAR|PARMRK
	                 |INPCK|ISTRIP|INLCR|IGNCR
	                 |ICRNL|IXON);
	tio.c_iflag |= (IGNBRK|IGNPAR);
	tio.c_oflag &= ~OPOST;
	tio.c_lflag &= ~(ECHO|ECHONL|ISIG|ICANON|IEXTEN);
	tio.c_cflag &= ~(CSIZE|PARENB|PARODD);
	tio.c_cflag |= CS8|CREAD|CLOCAL;
	cfsetspeed(&tio, SERIAL_SPEED);
	if (tcsetattr(fd, TCSANOW, &tio) < 0) {
		perror("tcsetattr");
		return -1;
	}
	return 0;
}

const char*
serial_device(const char *device) {
	if (device)
		return device;
	else
		return SERIAL_DEFAULT;
}

int
serial_open(const char *device)
{
	int fd;

	if (!device) {
		device = SERIAL_DEFAULT;
	}
	printf("opening device: %s\n", device);

	fd = open(device, O_RDWR);
	if (fd < 0) {
		perror("open");
		return -1;
	}
	if (serial_setup(fd) < 0) {
		close(fd);
		return -1;
	}
	return fd;
}
