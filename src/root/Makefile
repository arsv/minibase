include ../rules.mk

$(eval $(call command,runwith))
$(eval $(call system,findblk,8))
$(eval $(call system,switchroot,8))
$(eval $(call command,kmount,8))
$(eval $(call command,reboot,8))

findblk: findblk.o
