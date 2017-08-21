# Running make in this directory will build everything recursively.
# To build hello only, run "make hello".

bin = hello

include rules.mk

# Recursive stuff; single level atm, so here and not in rules.mk

subdirs = boot cmdops compat dcrypt events init msh net user wifi

define subcmd
$1 = $$(patsubst %,$1-%,$$(subdirs))

.SILENT: $$(patsubst %,$1-%,$$(subdirs))

$$($1): $1-%:
	$$(MAKE) -C $$* $2
endef

$(eval $(call subcmd,build))
$(eval $(call subcmd,clean,clean))
$(eval $(call subcmd,install,install))

all: $(build)
clean: $(clean)
install: $(install)
