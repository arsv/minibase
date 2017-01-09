/=../

include $/config.mk
include $/libdir.mk

all: all.a

libso = $(patsubst %.s,%.o,$(wildcard arch/$(ARCH)/*.[cs])) \
	$(patsubst %.c,%.o,$(wildcard *.c */*.c))

all.a: $(libso)
	$(AR) cr $@ $?

clean: clean-sub

clean-sub:
	rm -f */*.o arch/$(ARCH)/*.o all.a
