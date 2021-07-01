.SUFFIXES:

all: $(patsubst %.c,%.o,$(wildcard *.c))

include $/config.mk

clean = *.o *.d

%.o: %.c
	$(CC)$(if $(cflags), $(cflags)) -c $<

clean:
	rm -f $(clean)
