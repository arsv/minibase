initrd = findblk passblk 
command = dektool dmcrypt blkid deitool

include ../rules.mk

dektool: dektool.o keytool.o
deitool: deitool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

findblk: findblk.o findblk_config.o findblk_locate.o \
	findblk_fstest.o findblk_match.o

passblk: passblk.o findblk_config.o findblk_locate.o \
	findblk_fstest.o findblk_match.o passblk_mapper.o \
	passblk_input.o passblk_kfile.o passblk_term.o
