memsimctl
=========

### About

Command line tool to control
[memSIM2 EPROM simulator](http://momik.pl/memsim.htm).

### Building

    $ (g)make
    $ (g)make install

### Usage

    usage: memsimctl [-ehilp] [-d device] [-m memtype] [-o offset] [-r reset]
                     [-w file]
    
      -d device   serial device
      -e          enable emulation
      -h          print help
      -i          identify device
      -l          list memory types
      -m memtype  select memory type
      -o offset   specify input file offset
      -p          reset pulse positive, default: negative
      -r reset    reset pulse duration in ms, default: off
      -w file     write file to emulator

The input file (-w) has to be in raw binary format. The specified
offset (-o) determines the input file offset from where to start
reading. If the input file is smaller than the selected memory type,
the remaining memory will be filled with 0x00.

The -l switch lists available memory types and sizes:

    name    size
    2764      8K
    27128    16K
    27256    32K
    27512    64K
    27010   128K
    27020   256K
    27040   512K

If no memory size/type is selected (-m), the next larger memory type
is selected base on the input file size.

The -i switch sends an identify command to the device
("MI000..."). The response indicates the version and memory size of
the device:

    MI110000000000

### Platform support

memsimctl has been successfully tested on:

 * Fedora 33 (x86_64)
 * Ubuntu 20.04 (x86_64)
 
With limitation it also works on:

 * OpenBSD 6.8-current (x86_64)
 
The problem is that the device has to replugged after every command as
the FTDI driver or device in a non-working state afterwards.
