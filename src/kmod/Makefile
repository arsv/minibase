include ../rules.mk

$(eval $(call command,modinfo,1))
$(eval $(call command,modprobe,8))
$(eval $(call command,lsmod))

modinfo: modinfo.o common_map.o common_zip.o common_elf.o

modprobe: modprobe.o modprobe_dep.o modprobe_map.o modprobe_zip.o
