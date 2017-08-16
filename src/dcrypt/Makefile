/ = ../../

include $/config.mk

bin = dektool dmcrypt blkid findblk

include $/common.mk

dektool: dektool.o askpass.o keyfile.o
dmcrypt: dmcrypt.o askpass.o keyfile.o

findblk: findblk.o findblk_config.o findblk_udev.o \
	findblk_scan.o findblk_link.o findblk_match.o
