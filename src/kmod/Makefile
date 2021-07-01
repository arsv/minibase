/=../../

all = depmod modinfo modprobe lsmod

include ../rules.mk
include $/config.mk

modinfo: modinfo.o common_map.o common_zip.o common_elf.o

modprobe: modprobe.o common_map.o common_zip.o

depmod: depmod.o common_map.o common_zip.o common_elf.o

lsmod: lsmod.o

-include *.d
