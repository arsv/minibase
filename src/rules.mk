.SUFFIXES:
.SECONDARY:

/ := ../../

include $/config.mk

DESTDIR ?= ./out
clean = *.o *.d

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

# Common mkdir part for the several installation directories.
define destdir
dest$1dir := $$(DESTDIR)$$($1dir)

$$(dest$1dir):
	mkdir -p $$@
endef

$(eval $(call destdir,command))
$(eval $(call destdir,service))
$(eval $(call destdir,system))
$(eval $(call destdir,man1))
$(eval $(call destdir,man5))
$(eval $(call destdir,man8))

# Conditional for the "only" config option; $1=foo, $2=result
define ifenabled
$(if $(only),$(if $(filter $1,$(only)),$2),$2)
endef

# Define executable to be built and installed; $1=service, $2=foo
define register
all: $$(call ifenabled,$2,$2)

install-bin: $$(call ifenabled,$2,install-$2)

install-$2: $2 | $$(dest$1dir)
	$$(STRIP) -o $$(dest$1dir)/$2 $2

clean += $2

$$(if $3,$$(eval $$(call man,$2,$3)))
endef

# Registrable executables classes.
# The only difference is installation directory really.
service = $(call register,service,$1,$2)
command = $(call register,command,$1,$2)
system = $(call register,system,$1,$2)

# Define executables that shoudl be built but not installed; $1=foo
# (this is for auxillary development tools)
define other
all: $$(call ifenabled,$1,$1)

clean += $1
endef

# Man pages. Mostly used via `register` but there are also a couple
# of standalone section 5 pages.
define man
install-man: install-$1.$2

install-$1.$2: $1.$2 | $$(destman$2dir)
	cp -t $$(destman$2dir) $1.$2
endef

-include *.d
