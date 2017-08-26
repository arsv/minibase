.SUFFIXES:
.SECONDARY:

/ := $(dir $(wildcard ../configure ../../configure ../../../configure))

include $/config.mk

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

all: $(test)

run: $(test) $(patsubst %,run-%,$(test))

run-%:
	./$*

clean:
	rm -f *.o $(test)
