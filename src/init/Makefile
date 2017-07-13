/ = ../../

include $/config.mk

ifdef devel
CFLAGS += -DDEVEL

also = trap tick
endif

sbin = svmon svc init reboot

man1 =

include $/common.mk

svmon: svmon.o svmon_slot.o svmon_proc.o svmon_poll.o svmon_ctrl.o \
	svmon_cmds.o svmon_load.o svmon_heap.o svmon_warn.o

svc: svc.o svc_dump.o svc_util.o
