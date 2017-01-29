/ = ../../

include $/config.mk

ifdef devel
CFLAGS += -DDEVEL

also = trap tick
endif

sbin = svmon svc init reboot

man1 =

include $/common.mk

svmon: svmon.o \
	svmon_util.o svmon_proc.o svmon_poll.o svmon_ctrl.o svmon_load.o
