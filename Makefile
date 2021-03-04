
.PHONY: clean install style
.SUFFIXES:

PROG=		memsimctl
PREFIX?=	/usr/local

SRCS=		memsimctl.c
OBJS=		memsimctl.o
HDRS=		serial.h

SYSTEM!=	uname -s | tr 'A-Z' 'a-z' | sed -E 's/(open|free)//g'

SRCS+=		serial_$(SYSTEM).c
OBJS+=		serial_$(SYSTEM).o

CC?=		cc
CFLAGS+=	-Wall -Wextra

INSTALLDIR=	install -d
INSTALLBIN=	install -m 0555

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $(PROG) $(OBJS)

clean:
	rm -f $(OBJS) $(PROG)

install: $(PROG)
	$(INSTALLDIR) $(DESTDIR)$(PREFIX)/bin
	$(INSTALLBIN) $(PROG) $(DESTDIST)$(PREFIX)/bin/$(PROG)

style:
	astyle --options=astylerc $(SRCS) $(HDRS)

$(OBJS): $(HDRS)

.SUFFIXES: .c .o
.c.o:
	$(CC) $(CFLAGS) -c $<

