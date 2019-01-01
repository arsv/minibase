include ../rules.mk

$(eval $(call system,switchroot,8))
$(eval $(call system,reboot))
$(eval $(call service,svhub,8))
$(eval $(call command,svctl,1))
$(eval $(call service,badsrv))
$(eval $(call other,trap))
$(eval $(call other,tick))

svhub: svhub.o svhub_slot.o svhub_proc.o svhub_ctrl.o \
	svhub_load.o svhub_warn.o svhub_ring.o

svctl: svctl.o svctl_dump.o svctl_util.o
