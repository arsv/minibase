# Running make in this directory will build everything recursively.

.SUFFIXES:

/ := ../

include $/config.mk

subdirs := \
	apphub \
	cmd \
	cpio \
	ctool \
	dcrypt \
	debug \
	dhconf \
	elfinfo \
	hwids \
	ifmon \
	init \
	keymon \
	kmod \
	mpac \
	mpkg \
	msh \
	netcfg \
	ptyhub \
	rootfs \
	runcg \
	svchub \
	syslog \
	timed \
	udev \
	utils \
	vtmux \
	wsupp

build := $(patsubst %,build-%,$(subdirs)) 
clean := $(patsubst %,clean-%,$(subdirs))
strip := $(patsubst %,strip-%,$(subdirs))

build: $(build)
clean: $(clean)
strip: $(strip)

build-%:
	$(MAKE) -C $* build

clean-%:
	$(MAKE) -C $* clean

strip-%:
	$(MAKE) -C $* strip

.SILENT: $(build) $(clean) $(strip)
.PHONY: build clean strip

test:
	@echo $(subdirs)
