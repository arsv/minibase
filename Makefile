include config.mk

MAKEFLAGS += --no-print-directory
CFLAGS += -Ilibs -Ilibs/arch/$(ARCH)

all: libs.a build-admin build-common build-devel

libso = $(patsubst %.s,%.o,$(wildcard libs/arch/$(ARCH)/*.s)) \
	$(patsubst %.c,%.o,$(wildcard libs/*.c))

libs.a: $(libso)
	$(AR) cr $@ $?

build-%:
	$(MAKE) -C $*

install: install-admin install-common install-devel
install-%:
	$(MAKE) -C $* install

clean: clean-admin clean-common clean-libs
clean-%:
	$(MAKE) -C $* clean
clean-libs:
	rm -f libs/*.o libs/arch/*/*.o libs.a
