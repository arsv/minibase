include ../rules.mk

$(eval $(call sys,devinit,8))
$(eval $(call sys,udevmod,8))

devinit: devinit.o

udevmod: udevmod.o udevmod_alias.o udevmod_input.o
