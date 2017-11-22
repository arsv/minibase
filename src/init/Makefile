include ../rules.mk

$(eval $(call system,switchroot,8))
$(eval $(call system,reboot))
$(eval $(call service,super,8))
$(eval $(call command,svctl,1))
$(eval $(call service,badsrv))
$(eval $(call other,trap))
$(eval $(call other,tick))

super: super.o super_slot.o super_proc.o super_poll.o super_ctrl.o \
	super_cmds.o super_load.o super_heap.o super_warn.o super_ring.o

svctl: svctl.o svctl_dump.o svctl_util.o
