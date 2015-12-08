ARCH = x86_64

CROSS = 
CC = $(CROSS)gcc
AR = $(CROSS)gcc-ar
LD = $(CROSS)gcc
CFLAGS = -nostdinc -Wall -Os -g -fno-asynchronous-unwind-tables
ASFLAGS = -c -g
LDFLAGS = -nostdlib
LIBS = -lgcc
