include config.mk

MAKEFLAGS += --no-print-directory
CFLAGS += -Ilibs -Ilibs/arch/$(ARCH)

all: libs.a build

libso = $(patsubst %.s,%.o,$(wildcard libs/arch/$(ARCH)/*.s)) \
	$(patsubst %.c,%.o,$(wildcard libs/*.c))

libs.a: $(libso)
	$(AR) cr $@ $?

build: build-admin build-file build-text build-misc build-devel
build-%:
	$(MAKE) -C $*

install: install-admin install-file install-text install-misc install-devel
install-%:
	$(MAKE) -C $* install

clean: clean-libs clean-admin clean-file clean-text clean-misc clean-devel
clean-%:
	$(MAKE) -C $* clean
clean-libs:
	rm -f libs/*.o libs/arch/*/*.o libs.a
