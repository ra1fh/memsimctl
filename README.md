# memsimctl [![Build](https://github.com/ra1fh/memsimctl/actions/workflows/build.yml/badge.svg)](https://github.com/ra1fh/memsimctl/actions/workflows/build.yml)

Command line tool to control
[memSIM2 EPROM simulator](http://momik.pl/memsim.htm).

## Building

    $ make
    $ make install

## Platform Support

memsimctl has been successfully tested on:

 * Fedora 33 (x86_64)
 * Ubuntu 20.04 (x86_64)
 * OpenBSD 6.8/6.9 (x86_64)
 * FreeBSD 12.1 (x86_64)

## Usage

    usage: memsimctl [-d device] [-s start] [-r reset] [-z memfill] -m memtype -w file
           memsimctl [-d device] -m memtype -D
           memsimctl [-d device] -m memtype -E
           memsimctl [-d device] -i
           memsimctl -h
           memsimctl -L
    
      -d device     serial device
      -D            disable buffers
      -E            enable buffers
      -h            print help
      -i            identify device
      -L            list memory types
      -m memtype    select memory type
      -s start      start address where input file is loaded
      -r reset      reset in ms, < 0 negative, > 0 positve, = 0 off
      -v            verbose output
      -w file       write file to emulator
      -z memfill    fill value for unused memory

The following example demonstrates a few command line options to send
a 4K image.bin to the simulator in 2764 mode with a 100ms negative
reset pulse:

    $ memsimctl -m 2764 -r -100 -s 0x1000 -z 0xff -w image.bin
    
    [0x01000 : 0x01fff] (0x01000) image.bin
    [0x00000 : 0x01fff] (0x02000) EPROM 2764 0xff -100ms
    
    Transfer:   OK

The input file (-w) has to be in raw binary format. The specified
start address (-s) determines where the input is mapped into the
simulator memory.  The remaining memory will be filled with 0x00 or
the specified fill value (-z).

Enabling the output buffers is done implicitly when a new memory image
has been transmitted. The -D option can be used to disable output
buffers.

The -L option lists available memory types and sizes.

The -i option sends an identify command to the device. The response
indicates the version and memory size of the device:

    $ memsimctl -i
    Device:  /dev/ttyUSB0
    Version: 1
    Memory:  1

## Protocol Details

The following information has be gathered by observing the USB
communication. The description is likely incomplete.

### MI

The MI command can be used to identify the device and the version of
the device:

    MI000000000000\r\n

The device responds with:

    MI110000000000\r\n

The two digits seem to indicate version '1' and memory size '1'
(512KB).

### MC

The MC command is the configuration command that sets up various
parameters of the memSIM2 device. The buffer control is effective
immediately. The reset pulse is only issued after data transmission.

    MC0P123EN000FF\r\n
      ||^^^||
      || | |+----- presumably selftest (N=disable, unknown how to enable)
      || | +------ buffer control (D=disable, E=enable)
      || +-------- reset pulse time 123ms (255ms max)
      |+---------- reset polarity (P=positive, N=negative, 0=off)
      +----------- EPROM model selection

The response repeats the request string with 'X' for the self-test:

    MC0P123EX000FF\r\n

The following EPROM models are defined:

Model  | Protocol Value | Size
------:|---------------:|-------:
 2764  |             0  |   8 KB
27128  |             1  |  16 KB
27256  |             2  |  32 KB
27512  |             3  |  64 KB
27010  |             4  | 128 KB
27020  |             5  | 256 KB
27040  |             6  | 512 KB

### MD

The MD command starts the data transmission to the memSIM2 device.

    MD0512000000FF\r\n
       ^^^
        |
        +---------- data in KB

Following the header, the data is being transmitted in binary. The
device acknowledges successful transmission with:

    MD0512000000FF\r\n

After data transmission, the buffers are enabled immediately,
indicated by the "READY" LED. The reset pulse will be triggered as
well as configured with the previous reset command.
