.SUFFIXES:
.SECONDARY:

/ := $(dir $(wildcard ../configure ../../configure ../../../configure))

include $/config.mk

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

all: $(all)

clean:
	rm -f *.o $(all)
