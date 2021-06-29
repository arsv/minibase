.SUFFIXES:
.SECONDARY:
.PHONY: all strip clean

/ := ../../

include $/config.mk

all: $(all) $(aux)

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o $/lib.a
	$(LD) -o $@ $(filter %.o,$^)

$/lib.a:
	$(MAKE) -C $/lib

$/bin/%: % | $/bin
	$(STRIP) -o $@ $<

$/bin:
	mkdir -p $@

strip: $(patsubst %,$/bin/%,$(all))

clean:
	rm -f *.d *.o $(all) $(aux)

-include *.d
