include config.mk

MAKEFLAGS += --no-print-directory
CFLAGS += -Ilib -Ilib/arch/$(ARCH)

sdirs = admin devel text file misc

all: libs.a build

build: libs.a $(patsubst %,build-%,$(sdirs))

install: $(patsubst %,install-%,$(sdirs))

clean: clean-lib $(patsubst %,clean-%,$(sdirs))

libso = $(patsubst %.s,%.o,$(wildcard lib/arch/$(ARCH)/*.s)) \
	$(patsubst %.c,%.o,$(wildcard lib/*.c))

libs.a: $(libso)
	$(AR) cr $@ $?

build-%: | src/%
	$(MAKE) -C src/$*

install-%: | src/%
	$(MAKE) -C src/$* install

clean-lib:
	rm -f lib/*.o lib/*/*/*.o libs.a

clean-%: | src/%
	$(MAKE) -C src/$* clean
