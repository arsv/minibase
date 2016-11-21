include config.mk

MAKEFLAGS += --no-print-directory
CFLAGS += -Ilib -Iarch/$(ARCH) -Iarch/common

all: libs.a build

build: libs.a
	$(MAKE) -C src

install: $(patsubst %,install-%,$(sdirs))

clean: clean-lib $(patsubst %,clean-%,$(sdirs))

libso = $(patsubst %.s,%.o,$(wildcard arch/$(ARCH)/*.s)) \
	$(patsubst %.c,%.o,$(wildcard lib/*.c lib/*/*.c))

libs.a: $(libso)
	$(AR) cr $@ $?

install:
	$(MAKE) -C src install

clean: clean-lib clean-src

clean-src:
	rm -f src/*.o

clean-lib:
	rm -f arch/*/*.o lib/*.o lib/*/*.o libs.a
