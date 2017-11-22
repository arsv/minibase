include ../rules.mk

$(eval $(call service,lindevd))
$(eval $(call service,udevmod))
$(eval $(call initrd,udevmod))
$(eval $(call initrd,runwith))
$(eval $(call initrd,findblk))

$(eval $(call man,udevmod,8))
$(eval $(call man,lindevd,8))
$(eval $(call man,findblk,8))

udevmod: udevmod.o udevmod_alias.o udevmod_chmod.o

findblk: findblk.o findblk_config.o findblk_locate.o findblk_match.o
