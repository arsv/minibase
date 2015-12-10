.SUFFIXES:
.DEFAULT_GOAL = all

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

%.o: %.s
	$(AS) $(ASFLAGS) -o $@ $<

%: %.o $/libs.a
	$(LD) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LIBS)

ifdef /
$/libs.a:
	$(MAKE) -C $/ libs.a
endif

clean:
	rm -f *.o

distclean:
	rm -f $(all)
