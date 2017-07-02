.SUFFIXES:

include $/config.mk

%.o: %.s
	$(CC)$(if $(cflags), $(cflags)) -o $@ -c $<

%.o: %.c
	$(CC)$(if $(cflags), $(cflags)) -o $@ -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

all: $(patsubst %.c,%.o,$(wildcard *.c))

clean:
	rm -f *.o
