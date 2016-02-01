/ = ../
include ../config.mk

all = $(patsubst %.s,%.o,$(wildcard arch/$(ARCH)/*.s)) \
      $(patsubst %.c,%.o,$(wildcard *.c))

include ../common.mk

clean: clean-arch

clean-arch:
	rm -f arch/*/*.o
