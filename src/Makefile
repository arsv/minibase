# Running make in this directory will build everything recursively.
# To build hello only, run "make hello".

bin = hello

include rules.mk

subdirs = $(dir $(sort $(wildcard */Makefile)))

build := $(patsubst %,build-%,$(subdirs)) 
clean := $(patsubst %,clean-%,$(subdirs))
install := $(patsubst %,install-%,$(subdirs))

all: $(build)
clean: $(clean)
install: $(install)

build-%:
	$(MAKE) -C $*

clean-%:
	$(MAKE) -C $* clean

install-%:
	$(MAKE) -C $* install

.SILENT: $(build) $(clean) $(install)
