/=../../

all = depmod modinfo modprobe lsmod
aux = z-modconf z-loadmod

include ../rules.mk
include $/config.mk

modinfo: modinfo.o common_map.o common_zip.o common_elf.o

modprobe: modprobe.o common_map.o common_zip.o common_cnf.o

depmod: depmod.o common_map.o common_zip.o common_elf.o

lsmod: lsmod.o

z-modconf: z-modconf.o common_map.o common_zip.o common_cnf.o

z-loadmod: z-loadmod.o common_map.o common_zip.o

-include *.d
