LIBS = -L.. -ls -lgcc -ls
CFLAGS += -I../libs -I../libs/arch/$(ARCH)

.SUFFIXES:

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

%: %.o ../libs.a
	$(LD) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LIBS)

all: $(all)

../libs.a:
	$(MAKE) -C .. libs.a

clean:
	rm -f $(all) *.o

$(DESTDIR)/bin:
	mkdir -p $@

install: $(DESTDIR)/bin $(patsubst %,install-%,$(all)) installman
install-%:
	cp -a $* $(DESTDIR)/bin $(if $(STRIP),&& $(STRIP) $(DESTDIR)/bin/$*)

installman: installman-1 installman-8
installman-%:
	$(if $(wildcard *.$*),\
		mkdir -p $(DESTDIR)/$(man$*dir) && cp *.$* $(DESTDIR)/$(man$*dir))
