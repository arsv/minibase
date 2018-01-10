include ../rules.mk

$(eval $(call service,vtmux,8))
$(eval $(call command,vtctl,1))
$(eval $(call system,ctrlvt))
$(eval $(call system,stubvt))
$(eval $(call other,unlock))

vtmux: vtmux.o vtmux_pipe.o vtmux_exec.o vtmux_poll.o vtmux_slot.o \
	vtmux_vtsw.o vtmux_wait.o vtmux_ctrl.o

vtctl: vtctl.o vtctl_wire.o vtctl_dump.o
