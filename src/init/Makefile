/=../../

all = init rdinit

include ../rules.mk
include $/config.mk

init: init.o

rdinit: rdinit.o rdinit_root.o rdinit_udev.o

-include *.d
