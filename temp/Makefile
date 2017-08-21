include rules.mk

subdirs = $(dir $(sort $(wildcard */Makefile)))

define subcmd
$1 = $$(patsubst %,$1-%,$$(subdirs))

.SILENT: $$(patsubst %,$1-%,$$(subdirs))

$$($1): $1-%:
	$$(MAKE) -C $$* $2
endef

$(eval $(call subcmd,build))
$(eval $(call subcmd,clean,clean))

all: $(build)
clean: $(clean)
