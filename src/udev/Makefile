include ../rules.mk

$(eval $(call system,devinit,8))
$(eval $(call service,udevmod,8))

devinit: devinit.o

udevmod: udevmod.o udevmod_alias.o udevmod_input.o
