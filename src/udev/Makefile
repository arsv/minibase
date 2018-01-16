include ../rules.mk

$(eval $(call service,udevmod,8))
$(eval $(call command,runwith))
$(eval $(call system,findblk,8))

udevmod: udevmod.o udevmod_alias.o udevmod_chmod.o udevmod_input.o

findblk: findblk.o findblk_config.o findblk_locate.o findblk_match.o
