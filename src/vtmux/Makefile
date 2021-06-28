include ../rules.mk

$(eval $(call sys,vtmux))
$(eval $(call bin,vtctl))
$(eval $(call sys,ctrlvt))
$(eval $(call sys,stubvt))
$(eval $(call sys,xorgvt))
$(eval $(call sys,rootsh))

$(eval $(call skip,unlock))

vtmux: vtmux.o vtmux_pipe.o vtmux_exec.o vtmux_poll.o vtmux_slot.o \
	vtmux_vtsw.o vtmux_wait.o vtmux_ctrl.o

vtctl: vtctl.o
