/ = ../../

include $/config.mk

bin = svcmon svc init

man1 =

include $/common.mk

svcmon: svcmon.o \
	svcmon_util.o svcmon_proc.o svcmon_poll.o svcmon_ctrl.o svcmon_load.o

trap: trap.o

ifdef devel
CFLAGS += -DDEVEL
endif
