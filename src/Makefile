# Running make in this directory will build everything recursively.

.SUFFIXES:

/ := ../

include $/config.mk

subdirs := $(dir $(sort $(wildcard */Makefile)))

build := $(patsubst %,build-%,$(subdirs)) 
clean := $(patsubst %,clean-%,$(subdirs))
install := $(patsubst %,install-%,$(subdirs))

all: $(build)
clean: $(clean)
install: $(if $(DESTDIR),$(install),no-destdir-install)

build-%:
	$(MAKE) -C $*

clean-%:
	$(MAKE) -C $* clean

install-%:
	$(MAKE) -C $* DESTDIR=$(abspath $(DESTDIR)) install

.SILENT: $(build) $(clean) $(install)

define subtargets
$1%.o:
	$$(MAKE) -C $$(dir $$@) $$(notdir $$@)
endef

$(foreach d,$(subdirs),$(eval $(call subtargets,$d)))

test:
	@echo $(subdirs)
