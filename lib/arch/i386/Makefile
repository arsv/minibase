/=../../../

include $/config.mk

all: _start.o sigreturn.o syscall.o

%.o: %.s
	$(CC) -o $@ -c $<

clean:
	rm -f *.o

.PHONY: all clean
