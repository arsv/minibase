/ = ../../

include $/config.mk

.SUFFIXES:

.SECONDARY:

%.o: %.c
	$(CC)$(if $(CFLAGS), $(CFLAGS)) -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

run: $(test) $(patsubst %,run-%,$(test))

run-%:
	./$*

all: $(test)

clean:
	rm -f *.o $(test)
