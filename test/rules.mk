.SUFFIXES:
.SECONDARY:

%: %.o $/lib/all.a
	$(LD) -o $@ $(filter %.o,$^)

all: $(test)

run: $(test) $(patsubst %,run-%,$(test))

run-%:
	$(if $(QEMU),$(QEMU) )./$*

$/lib/all.a:
	$(MAKE) -C $(dir $@)

clean:
	rm -f *.o *.d $(test)
