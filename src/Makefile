# Running make in this directory will build everything recursively.
# To build hello only, run "make hello".

.SUFFIXES:

/ := ../

include $/config.mk

subdirs := $(dir $(sort $(wildcard */Makefile)))

build := $(patsubst %,build-%,$(subdirs)) 
clean := $(patsubst %,clean-%,$(subdirs)) clean-hello
install := $(patsubst %,install-%,$(subdirs))

all: hello $(build)
clean: $(clean)
install: $(if $(DESTDIR),$(install),no-destdir-install)

build-%:
	$(MAKE) -C $*

clean-%:
	$(MAKE) -C $* clean

install-%:
	$(MAKE) -C $* DESTDIR=$(abspath $(DESTDIR)) install

.SILENT: $(build) $(clean) $(install)

hello: hello.o
	$(CC) -o $@ $<

hello.o: hello.c
	$(CC)$(if $(CFLAGS), $(CLFAGS)) -c $<

clean-hello:
	rm -f hello hello.o hello.d

define subtargets
$1%.o:
	$$(MAKE) -C $$(dir $$@) $$(notdir $$@)
endef

$(foreach d,$(subdirs),$(eval $(call subtargets,$d)))

test:
	@echo $(subdirs)
