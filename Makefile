MAKEFLAGS += --no-print-directory

include config.mk

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

distclean = $(patsubst %,distclean-%,$(subdirs))
distclean: $(distclean)
$(distclean): distclean-%:
	$(MAKE) -C $* distclean

clean-libs:
	rm -f libs/*.o libs/arch/*/*.o libs.a
