all = vtmux vtctl ctrlvt xorgvt rootsh
aux = unlock

include ../rules.mk

vtmux: vtmux.o vtmux_pipe.o vtmux_exec.o vtmux_poll.o vtmux_slot.o \
	vtmux_vtsw.o vtmux_wait.o vtmux_ctrl.o

vtctl: vtctl.o
