include ../rules.mk

$(eval $(call command,modprobe,8))
$(eval $(call command,lsmod))

modprobe: modprobe.o modprobe_dep.o modprobe_map.o
