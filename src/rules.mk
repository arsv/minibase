.SUFFIXES:
.SECONDARY:

/ := $(dir $(wildcard ../configure ../../configure ../../../configure))

include $/config.mk

DESTDIR ?= ./out
clean = *.o

all:

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

# Quick explanation, say for bin="ls echo":
#
#   dstdir-bin := ./out/bin
#   target-bin := ./out/bin/ls ./out/bin/echo
#   mkdirs += ./out/bin
#
#   ./out/bin/{ls,echo}: ./out/bin/%: % | ./out/bin
#       strip -o $@ $<
#
#   ./out/bin:         # <-- actually via $(mkdirs)
#       mkdir -p $@
#
# In actual runs DESTDIR is always an absolute path.

define bin-rules
dstdir-$1 := $$(DESTDIR)$$($1dir)
target-$1 := $$(patsubst %,$$(dstdir-$1)/%,$$($1))
mkdirs += $$(dstdir-$1)
clean += $$($1)

all: all-$1

all-$1: $$($1)

install: install-$1

install-$1: $$(target-$1) | $$(dstdir-$1)

$$(target-$1): $$(dstdir-$1)/%: % | $$(dstdir-$1)
	$$(STRIP) -o $$@ $$(notdir $$@)

.PHONY: $$(target-$1)
endef

targets = command service system initrd

$(foreach _,$(targets),$(if $($_),$(eval $(call bin-rules,$_))))

define man-rules
dstdir-$1 := $$(DESTDIR)$$($1dir)
target-$1 := $$(patsubst %,$$(dstdir-$1)/%,$$($1))
mkdirs += $$(dstdir-$1)

install: install-$1

install-man: install-$1

install-$1: $$(target-$1)

$$(target-$1): $$(dstdir-$1)/%: % | $$(dstdir-$1)
	cp -t $$(dir $$@) $$(notdir $$@)

.PHONY: $$(target-$1)
endef

$(foreach _, 1 5 8, $(eval man$_ := $(sort $(wildcard *.$_))))
$(foreach _, 1 5 8, $(if $(man$_),$(eval $(call man-rules,man$_,$_))))

$(sort $(mkdirs)): %:
	mkdir -p $@

all: $(other)

clean += $(other)

clean:
	rm -f $(clean)
