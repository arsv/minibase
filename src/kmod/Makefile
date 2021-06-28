include ../rules.mk

$(eval $(call bin,depmod))
$(eval $(call bin,modinfo))
$(eval $(call bin,modprobe))
$(eval $(call bin,lsmod))

modinfo: modinfo.o common_map.o common_zip.o common_elf.o

modprobe: modprobe.o common_map.o common_zip.o

depmod: depmod.o common_map.o common_zip.o common_elf.o
