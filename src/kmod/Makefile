include ../rules.mk

$(eval $(call bin,depmod,1))
$(eval $(call bin,modinfo,1))
$(eval $(call bin,modprobe,1))
$(eval $(call bin,lsmod,1))

modinfo: modinfo.o common_map.o common_zip.o common_elf.o

modprobe: modprobe.o common_map.o common_zip.o

depmod: depmod.o common_map.o common_zip.o common_elf.o
