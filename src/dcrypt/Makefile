/ = ../../

include $/config.mk

bin = dektool dmcrypt blkid findblk passblk

include $/common.mk

dektool: dektool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

findblk: findblk.o findblk_config.o findblk_udev.o \
	findblk_scan.o findblk_match.o

passblk: passblk.o passblk_config.o passblk_term.o passblk_locate.o \
	passblk_mapper.o passblk_match.o passblk_input.o \
	passblk_kfile.o passblk_fstest.o
