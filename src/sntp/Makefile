include ../rules.mk

$(eval $(call service,timed))
$(eval $(call command,tictl))

timed: timed.o timed_ctrl.o timed_poll.o timed_sync.o

tictl: tictl.o
