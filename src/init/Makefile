/ = ../../

include $/config.mk

ifdef devel
CFLAGS += -DDEVEL

also = trap tick
endif

sbin = init reboot super svc

man1 =

include $/common.mk

super: super.o super_slot.o super_proc.o super_poll.o super_ctrl.o \
	super_cmds.o super_load.o super_heap.o super_warn.o super_ring.o

svc: svc.o svc_dump.o svc_util.o
