system = start super reboot
command = svctl
other = trap tick

include ../rules.mk

super: super.o super_slot.o super_proc.o super_poll.o super_ctrl.o \
	super_cmds.o super_load.o super_heap.o super_warn.o super_ring.o

svctl: svctl.o svctl_dump.o svctl_util.o
