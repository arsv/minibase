/=./

include config.mk

DESTDIR ?= ./out

.SUFFIXES:

all: lib.a build strip

libs: lib.a
bins: strip

lib.a:
	$(MAKE) -C lib

build: lib.a
	$(MAKE) -C src

strip:
	$(MAKE) -C src strip

clean: clean-lib clean-src clean-test clean-temp

clean-lib:
	rm -f lib.a
	$(MAKE) -C lib clean

clean-%:
	$(MAKE) -C $* clean

test:
	$(MAKE) -C test run

.PHONY: test strip
.SILENT: build lib.a clean-lib clean-src clean-test

# Allow building files from the top dir
# Usefule for :make in vim

src/%.o lib/%.o temp/%.o test/%.o:
	$(MAKE) -C $(dir $@) $(notdir $@)
