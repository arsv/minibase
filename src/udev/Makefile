service = lindevd udevmod
initrd = udevmod runwith findblk

include ../rules.mk

udevmod: udevmod.o udevmod_alias.o udevmod_chmod.o

findblk: findblk.o findblk_config.o findblk_locate.o \
	findblk_fstest.o findblk_match.o

