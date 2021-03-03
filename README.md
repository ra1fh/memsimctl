
[![Build](https://github.com/ra1fh/memsimctl/actions/workflows/build.yml/badge.svg)](https://github.com/ra1fh/memsimctl/actions/workflows/build.yml)

memsimctl
=========

### About

Command line tool to control
[memSIM2 EPROM simulator](http://momik.pl/memsim.htm).

### Building

    $ (g)make
    $ (g)make install

### Usage

    usage: memsimctl [-d device] [-s start] [-r reset] [-z memfill] -m memtype -w file
           memsimctl [-d device] -i
           memsimctl -h
           memsimctl -L
    
      -d device     serial device
      -h            print help
      -i            identify device
      -L            list memory types
      -m memtype    select memory type
      -s start      start address where input file is loaded
      -r reset      reset in ms, < 0 negative, > 0 positve, = 0 off
      -v            verbose output
      -w file       write file to emulator
      -z memfill    fill value for unused memory

The input file (-w) has to be in raw binary format. The specified
start address (-s) determines where the input is mapped into the
simulator memory.  If the input file is smaller than the selected
memory type, the remaining memory will be filled with 0x00 or the
specified fill value (-z).

The -L switch lists available memory types and sizes:

    name    size
    2764      8K
    27128    16K
    27256    32K
    27512    64K
    27010   128K
    27020   256K
    27040   512K

The -i switch sends an identify command to the device
("MI000..."). The response indicates the version and memory size of
the device:

    $ memsimctl -i
    Device: /dev/ttyUSB0
    Version: 1
    Memory: 1

Example that writes image.bin to the simulator, enables simulation
buffers and sends a 200ms positive reset pulse (for the reset to work
the reset clip has to be connected):

    $ memsimctl -r 200 -m 2764 -w image.bin
    
    image.bin: [0x000000 : 0x0007ff] (0x0800)
    
    EPROM:    2764 (0x2000)
    Fill:     0x00
    Reset:     200 ms
    Transfer:   OK (0x2000)

### Status LEDs

The memsim2 device has three LEDs:

 * Green "TRANSMISSION": indicates USB transfer
 * Yellow "READY": indicates buffer enabled (off when disabled)
 * Red "RUN": indicates memory access from the target device

### Platform Support

memsimctl has been successfully tested on:

 * Fedora 33 (x86_64)
 * Ubuntu 20.04 (x86_64)

With limitation it also works on:

 * OpenBSD 6.8-current (x86_64), USB re-plug necessary after every command

### Protocol Details

The following information has be gathered by observing the USB
communication. The description is likely incomplete.

#### MI

The MI command can be used to identify the device and the version of
the device:

    MI000000000000\r\n

The device responds with:

    MI110000000000\r\n

The two digits seem to indicate version '1' and memory size '1'
(512KB).

#### MC

The MC command is the configuration command that sets up various
parameters of the memSIM2 device. The buffer control is effective
immediately. The reset pulse is only issued after data transmission.

    MC0P234DN00000\r\n
      ||^^^||
      || | |+----- presumably selftest (N=disable, unknown how to enable)
      || | +------ buffer control (D=disable, E=enable)
      || +-------- reset pulse time 234ms (255ms max)
      |+---------- reset polarity (P=positive, N=negative, 0=off)
      +----------- EPROM model selection

The following EPROM models are defined:

Model  | Size | Protocol Value
------:|-----:|--------------:
2764   |   0  |   8 KB
27128  |   1  |  16 KB
27256  |   2  |  32 KB
27512  |   3  |  64 KB
27010  |   4  | 128 KB
27020  |   5  | 256 KB
27040  |   6  | 512 KB

The response repeats the request string with 'X' for the self-test:

    MC0P234DN00000\r\n

#### MD

The MD command starts the data transmission to the memSIM2 device.

    MD051200000000\r\n
       ^^^
        |
        +---------- data in KB

Following the header, the data is being transmitted in binary. The
device acknowledges successful transmission with:

    MD051200000000\r\n

After data transmission, the buffers are enabled immediately,
indicated by the "READY" LED. The reset pulse will be triggered as
well as configured with the previous reset command.
