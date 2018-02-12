.SUFFIXES:
.SECONDARY:

/ := ../../

include $/config.mk

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o %/lib/all.a
	$(LD) -o $@ $(filter %.o,$^)

all: $(test)

run: $(test) $(patsubst %,run-%,$(test))

run-%:
	$(if $(QEMU),$(QEMU) )./$*

clean:
	rm -f *.o *.d $(test)

-include *.d
