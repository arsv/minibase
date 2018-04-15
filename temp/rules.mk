.SUFFIXES:
.SECONDARY:

/ := ../../

include $/config.mk

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o $/lib/all.a
	$(LD) -o $@ $(filter %.o,$^)

all: $(all)

clean:
	rm -f *.o *.d $(all)

$/lib/all.a:
	$(MAKE) -C $(dir $@)

-include *.d
