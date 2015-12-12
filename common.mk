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
