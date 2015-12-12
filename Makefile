include config.mk

MAKEFLAGS += --no-print-directory
CFLAGS += -Ilibs -Ilibs/arch/$(ARCH)

subdirs = base misc root

all: libs.a $(subdirs)

libso = $(patsubst %.s,%.o,$(wildcard libs/arch/$(ARCH)/*.s)) \
	$(patsubst %.c,%.o,$(wildcard libs/*.c))

libs.a: $(libso)
	$(AR) cr $@ $?

.PHONY: $(subdirs)

$(subdirs):
	$(MAKE) -C $@

clean = $(patsubst %,clean-%,$(subdirs))
clean: $(clean) clean-libs
$(clean): clean-%:
	$(MAKE) -C $* clean

clean-libs:
	rm -f libs/*.o libs/arch/*/*.o libs.a
