/=../../

all: $(patsubst %.c,%.o,$(wildcard *.c))

include $/config.mk

%.o: %.c
	$(CC)$(if $(cflags), $(cflags)) -c $<

clean:
	rm -f *.d *.o

-include *.d
