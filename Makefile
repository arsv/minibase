/=./

include config.mk

MAKEFLAGS += --no-print-directory

all: libs build

libs: lib/all.a

lib/all.a:
	$(MAKE) -C lib

build: lib/all.a
	$(MAKE) -C src

install:
	$(MAKE) -C src install

clean: clean-lib clean-src clean-test

clean-%:
	$(MAKE) -C $* clean

test:
	$(MAKE) -C test run

.PHONY: test
