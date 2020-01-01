.SUFFIXES:
.SECONDARY:

/ := ../../

include $/config.mk

DESTDIR ?= $/out
clean = *.o *.d

bindir = /bin
sysdir = /sys
mandir = /man

destbin = $(DESTDIR)$(bindir)
destsys = $(DESTDIR)$(sysdir)
destman = $(DESTDIR)$(mandir)

all:

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o $/lib/all.a
	$(LD) -o $@ $(filter %.o,$^)

clean:
	rm -f $(clean)

install: install-bin install-man

install-bin:
install-man:

$/lib/all.a:
	$(MAKE) -C $(dir $@)

$(destbin) $(destsys) $(destman):
	mkdir -p $@

# --- --- --- ---

define bin
all: $1

install-bin: install-$1

install-$1: $1 | $$(destbin)
	$$(STRIP) -o $$(destbin)/$1 $1

clean += $1

$$(if $2,$$(eval $$(call manpage,$1,$2)))
endef

# --- --- --- ---

define sys
all: $1

install-bin: install-$1

install-$1: $1 | $$(destsys)
	$$(STRIP) -o $$(destsys)/$1 $1

clean += $1

$$(if $2,$$(eval $$(call manpage,$1,$2)))
endef

# --- --- --- ---

define skip
all: $1

clean += $1
endef

# --- --- --- ---

define manpage
install-man: install-$1.$2

install-$1.$2: $1.$2 | $$(destman)
	cp -t $$(destman) $1.$2
endef

# --- --- --- ---

-include *.d
