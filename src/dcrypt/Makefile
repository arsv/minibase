bin = dektool dmcrypt blkid

sbin = findblk passblk

man5 = blktab.5
man8 = blkid.8 findblk.8 passblk.8 dektool.8 dmcrypt.8

include ../rules.mk

dektool: dektool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

findblk: findblk.o findblk_config.o findblk_locate.o \
	findblk_fstest.o findblk_match.o

passblk: passblk.o findblk_config.o findblk_locate.o \
	findblk_fstest.o findblk_match.o passblk_mapper.o \
	passblk_input.o passblk_kfile.o passblk_term.o
