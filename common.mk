.SUFFIXES:

.SECONDARY:

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

all: $(bin) $(sbin) $(also)

clean:
	rm -f *.o $(bin) $(sbin) $(also)

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
