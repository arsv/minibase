include ../rules.mk

$(eval $(call service,svchub,8))
$(eval $(call command,svcctl,1))
$(eval $(call service,badsrv))

$(eval $(call other,trap))
$(eval $(call other,tick))

svchub: svchub.o svchub_control.o svchub_reload.o svchub_output.o \
	svchub_monitor.o svchub_reboot.o

svcctl: svcctl.o
