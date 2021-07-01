.SECONDARY:
.PHONY: all strip clean

all: $(all) $(aux)

build: all strip

%: %.o $/lib.a
	$(LD) -o $@ $(filter %.o,$^)

$/bin/%: % | $/bin
	$(STRIP) -o $@ $<

$/bin:
	mkdir -p $@

strip: $(patsubst %,$/bin/%,$(all))

clean:
	rm -f *.d *.o $(all) $(aux)
