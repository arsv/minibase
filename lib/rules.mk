.SUFFIXES:

/ := $(dir $(wildcard ../configure ../../configure ../../../configure))
objs = $(patsubst %.c,%.o,$(sort $(wildcard *.c)))

include $/config.mk

clean = *.o

objs: $(objs)

$(objs): %.o: %.c
	$(CC)$(if $(cflags), $(cflags)) -c $<

clean:
	rm -f $(clean)
