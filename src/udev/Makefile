include ../rules.mk

$(eval $(call sys,devinit))
$(eval $(call sys,udevmod))

devinit: devinit.o

udevmod: udevmod.o udevmod_alias.o udevmod_input.o
