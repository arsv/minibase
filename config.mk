ARCH = x86_64

CROSS = 
CC = $(CROSS)gcc
AR = $(CROSS)ar
LD = $(CC)
AS = $(CC)
CFLAGS = -nostdinc -Wall -Os -g -fno-asynchronous-unwind-tables
ASFLAGS = -g -c
LDFLAGS = -nostdlib

STRIP = $(CROSS)strip

bindir = /bin
man1dir = /man/man1
man8dir = /man/man8
