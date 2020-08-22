include ../rules.mk

$(eval $(call sys,timed))
$(eval $(call bin,tictl))

timed: timed.o timed_ctrl.o timed_poll.o timed_sync.o

tictl: tictl.o
