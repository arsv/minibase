.SUFFIXES:

subdirs := $(dir $(sort $(wildcard */Makefile)))

build := $(patsubst %,build-%,$(subdirs))
clean := $(patsubst %,clean-%,$(subdirs))

all: $(build)
clean: $(clean)

build-%:
	$(MAKE) -C $*

clean-%:
	$(MAKE) -C $* clean

.SILENT: $(build) $(clean)

define subtargets
$1%.o:
	$$(MAKE) -C $$(dir $$@) $$(notdir $$@)
endef

$(foreach d,$(subdirs),$(eval $(call subtargets,$d)))
