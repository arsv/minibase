include ../rules.mk

$(eval $(call service,mountd,8))
$(eval $(call command,pmount,1))
$(eval $(call command,kmount,8))
$(eval $(call system,reboot))
$(eval $(call command,locfg,8))

mountd: mountd.o mountd_ctrl.o mountd_type.o mountd_loop.o
