# Running make in this directory will build everything recursively.

.SUFFIXES:

/ := ../

include $/config.mk

subdirs := $(dir $(sort $(wildcard */Makefile)))

build := $(patsubst %,build-%,$(subdirs)) 
clean := $(patsubst %,clean-%,$(subdirs))
strip := $(patsubst %,strip-%,$(subdirs))

all: $(build)
clean: $(clean)
strip: $(strip)

build-%:
	$(MAKE) -C $*

clean-%:
	$(MAKE) -C $* clean

strip-%:
	$(MAKE) -C $* strip

.SILENT: $(build) $(clean) $(strip)

test:
	@echo $(subdirs)
