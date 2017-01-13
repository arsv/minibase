/ = ../../

include $/config.mk

bin = init telinit

man1 =

include $/common.mk

init: init.o \
	init_load.o init_recs.o init_poll.o \
	init_pass.o init_wait.o init_ctrl.o \
	init_cmds.o init_warn.o

trap: trap.o
