/ = ../../

include $/config.mk

bin = svcmon svc init

man1 =

include $/common.mk

svcmon: svcmon.o \
	svcmon_load.o svcmon_recs.o svcmon_poll.o \
	svcmon_pass.o svcmon_wait.o svcmon_ctrl.o \
	svcmon_cmds.o svcmon_warn.o svcmon_heap.o

trap: trap.o

ifdef devel
CFLAGS += -DDEVEL
endif
