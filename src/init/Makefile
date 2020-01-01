include ../rules.mk

$(eval $(call sys,svchub,8))
$(eval $(call bin,svcctl,1))

$(eval $(call skip,z-badsrv))
$(eval $(call skip,z-trap))
$(eval $(call skip,z-tick))

svchub: svchub.o svchub_control.o svchub_reload.o svchub_output.o \
	svchub_monitor.o svchub_reboot.o

svcctl: svcctl.o
