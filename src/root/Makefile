include ../rules.mk

$(eval $(call system,findblk,8))
$(eval $(call command,blkhwid,8))
$(eval $(call system,switchroot,8))
$(eval $(call command,kmount,8))
$(eval $(call command,reboot))

findblk: findblk.o
