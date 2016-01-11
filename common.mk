/ ?= ../../

LIBS = -L$/ -ls -lgcc -ls
CFLAGS += -I$/lib -I$/lib/arch/$(ARCH)

.SUFFIXES:

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

%: %.o $/libs.a
	$(LD) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LIBS)

all: $(bin) $(sbin)

$/libs.a:
	$(MAKE) -C $/ libs.a

clean:
	rm -f $(bin) $(sbin) *.o

$(DESTDIR)/bin $(DESTDIR)/sbin:
	mkdir -p $@

install = $(foreach _, bin sbin man1 man8, $(if $($_),install-$_))
install: $(install)
install-man: $(filter install-man%,$(install))

install-bin install-sbin: install-%:
	  mkdir -p $(DESTDIR)$($*dir)
	  cp -a $($*) $(DESTDIR)$($*dir)
	  $(STRIP) $(patsubst %,$(DESTDIR)$($*dir)/%,$($*))

install-man%:
	mkdir -p $(DESTDIR)$(man$*dir)
	cp $(man$*) $(DESTDIR)$(man$*dir)
