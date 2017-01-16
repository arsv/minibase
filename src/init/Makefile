/ = ../../

include $/config.mk

ifdef devel
CFLAGS += -DDEVEL

also = trap tick
endif

sbin = svcmon svc init reboot

man1 =

include $/common.mk

svcmon: svcmon.o \
	svcmon_util.o svcmon_proc.o svcmon_poll.o svcmon_ctrl.o svcmon_load.o
