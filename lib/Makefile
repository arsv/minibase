/=../

all: $(patsubst %.c,%.o,$(wildcard *.c))

include $/config.mk

clean:
	rm -f *.d *.o

-include *.d
