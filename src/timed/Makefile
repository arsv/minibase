/ = ../../

all = timed tictl

include ../rules.mk
include $/config.mk

timed: timed.o timed_ctrl.o timed_poll.o timed_sync.o

tictl: tictl.o

-include *.d
