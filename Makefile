include config.mk
include common.mk
CFLAGS += -I$/libs -I$/libs/arch/$(ARCH)

all = hello mountvfs

all: $(all)

hello: hello.o

mountvfs: mountvfs.o

%: %.o libs.a
	$(LD) $(LDFLAGS) -o $@ $(filter %.o,$^) libs.a $(LIBS) libs.a

libso = $(patsubst %.s,%.o,$(wildcard libs/arch/$(ARCH)/*.s)) \
	$(patsubst %.c,%.o,$(wildcard libs/*.c))

libs.a: $(libso)
	$(AR) cr $@ $?

clean:
	rm -f *.o libs/*.o libs/arch/*/*.o libs.a

distclean: clean
	rm -f $(all)
