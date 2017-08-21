subdirs := $(dir $(sort $(wildcard */Makefile)))

build := $(patsubst %,build-%,$(subdirs))
clean := $(patsubst %,clean-%,$(subdirs))

all: $(build)
clean: $(clean)

include rules.mk

build-%:
	$(MAKE) -C $*

clean-%:
	$(MAKE) -C $* clean

.SILENT: $(build) $(clean)
