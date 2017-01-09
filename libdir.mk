.SUFFIXES:

include $/config.mk

%.o: %.s
	$(CC) $(CFLAGS) -o $@ -c $<

%.o: %.c
	$(CC) $(CFLAGS) -o $@ -c $<

%: %.o
	$(LD) -o $@ $(filter %.o,$^) $(LIBS)

all: $(patsubst %.c,%.o,$(wildcard *.c))

clean:
	rm -f *.o
