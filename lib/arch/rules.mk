.SUFFIXES:
.SILENT: objs

/=../../../

include $/config.mk

%.o: %.s
	$(CC)$(if $(cflags), $(cflags)) -o $@ -c $<

objs: $(patsubst %.s,%.o,$(wildcard *.s))

clean:
	rm -f *.o
