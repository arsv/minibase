.SUFFIXES:

/=../../../

include $/config.mk

%.o: %.s
	$(CC)$(if $(cflags), $(cflags)) -o $@ -c $<

all: $(patsubst %.s,%.o,$(wildcard *.s))

clean:
	rm -f *.o
