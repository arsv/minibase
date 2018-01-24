.SUFFIXES:

dirs = arch string format date crypto

all: $(patsubst %,all-%,$(dirs))

run: $(patsubst %,run-%,$(dirs))

clean: $(patsubst %,clean-%,$(dirs))

all-%: %/Makefile
	$(MAKE) -C $* all

run-%: %/Makefile
	$(MAKE) -C $* run

clean-%: %/Makefile
	$(MAKE) -C $* clean

define subtargets
$1/%.o:
	$$(MAKE) -C $$(dir $$@) $$(notdir $$@)
endef

$(foreach d,$(dirs),$(eval $(call subtargets,$d)))
