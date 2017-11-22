initrd = passblk 
command = dektool dmcrypt blkid deitool

include ../rules.mk

dektool: dektool.o keytool.o
deitool: deitool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

passblk: passblk.o passblk_config.o passblk_locate.o \
	passblk_fstest.o passblk_match.o passblk_mapper.o \
	passblk_input.o passblk_kfile.o passblk_term.o
