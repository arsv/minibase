/ = ../../

include $/config.mk

bin = vtmux vtctl unlock

man1 =

include $/common.mk

vtmux: vtmux.o vtmux_pipe.o vtmux_exec.o vtmux_poll.o vtmux_slot.o \
	vtmux_vtsw.o vtmux_ctrl.o vtmux_cmds.o

vtctl: vtctl.o vtctl_wire.o
