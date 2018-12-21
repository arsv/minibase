include ../rules.mk

$(eval $(call command,depmod,1))
$(eval $(call command,modinfo,1))
$(eval $(call command,modprobe,1))
$(eval $(call command,lsmod,1))

modinfo: modinfo.o common_map.o common_zip.o common_elf.o

modprobe: modprobe.o common_map.o common_zip.o

depmod: depmod.o common_map.o common_zip.o common_elf.o
