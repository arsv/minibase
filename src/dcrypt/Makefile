/ = ../../

include $/config.mk

bin = dektool dmcrypt blkid findblk

include $/common.mk

dektool: dektool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

findblk: findblk.o findblk_config.o findblk_udev.o \
	findblk_scan.o findblk_match.o
