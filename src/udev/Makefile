/ = ../../

all = rdinit udevmod

include ../rules.mk
include $/config.mk

rdinit: rdinit.o rdinit_root.o rdinit_udev.o

udevmod: udevmod.o udevmod_alias.o udevmod_input.o

-include *.d
