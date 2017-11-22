.SUFFIXES:
.SECONDARY:

/ := ../../

include $/config.mk

DESTDIR ?= ./out
clean = *.o

all:

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

clean:
	rm -f $(clean)

install: install-bin install-man

install-bin:
install-man:

# --- Definitions
define destdir
dest$1dir := $$(DESTDIR)$$($1dir)

$$(dest$1dir):
	mkdir -p $$@
endef

$(eval $(call destdir,command))
$(eval $(call destdir,service))
$(eval $(call destdir,system))
$(eval $(call destdir,initrd))
$(eval $(call destdir,man1))
$(eval $(call destdir,man5))
$(eval $(call destdir,man8))

# --- 1=service, 2=foo
define register
all: $2

install-bin: install-$1-$2

install-$1-$2: $2 | $$(dest$1dir)
	$$(STRIP) -o $$(dest$1dir)/$2 $2

clean += $2

$$(if $3,$$(eval $$(call man,$2,$3)))
endef

service = $(call register,service,$1,$2)
command = $(call register,command,$1,$2)
initrd = $(call register,initrd,$1,$2)

# --- other foo
define other
all: $1

clean += $1
endef

# --- man foo 1
define man
install-man: install-man-$1

install-man-$1: $1.$2 | $$(destman$2dir)
	cp -t $$(destman$2dir) $1.$2
endef
