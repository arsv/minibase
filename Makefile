/=./

include config.mk

libdirs = crypto format netlink nlusctl string time util
libpatt = lib/arch/$(ARCH)/*.o lib/*.o $(patsubst %,lib/%/*.o,$(libdirs))

all: libs
	$(MAKE) bins

libs: build-lib-arch build-lib $(patsubst %,build-lib-%,$(libdirs))
	$(MAKE) lib.a

build-lib:
	$(MAKE) -C lib

build-lib-arch:
	$(MAKE) -C lib/arch/$(ARCH)

build-lib-%:
	$(MAKE) -C lib/$*

lib.a: $(wildcard $(libpatt))
	ar crDT $@ $^

build:
	$(MAKE) -C src

bins:
	$(MAKE) -C src build

clean: clean-lib clean-src clean-test clean-temp

clean-lib:
	rm -f lib.a
	rm -f lib/*.o lib/*/*.o lib/arch/*/*.o
	rm -f lib/*.d lib/*/*.d lib/arch/*/*.d

clean-src:
	$(MAKE) -C src clean

clean-test:
	$(MAKE) -C test clean

clean-temp:
	$(MAKE) -C temp clean

test:
	$(MAKE) -C test run

# Allow building files from the top dir
# Useful for :make in vim

src/%.o lib/%.o temp/%.o test/%.o:
	$(MAKE) -C $(dir $@) $(notdir $@)

.PHONY: all build strip libs test
.PHONY: clean clean-lib clean-src clean-temp clean-test
