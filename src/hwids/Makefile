/=../../

all = lsdri lspci lsusb inputs

include ../rules.mk
include $/config.mk

lsdri: lsdri.o
lspci: lspci.o
lsusb: lsusb.o

inputs: inputs.o inputs_keys.o inputs_misc.o

-include *.d
