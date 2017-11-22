include ../rules.mk

$(eval $(call initrd,passblk,8))
$(eval $(call command,dektool,8))
$(eval $(call command,deitool,8))
$(eval $(call command,dmcrypt,8))
$(eval $(call command,blkid,8))

$(eval $(call man,blktab,5))

dektool: dektool.o keytool.o
deitool: deitool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

passblk: passblk.o passblk_config.o passblk_locate.o \
	passblk_fstest.o passblk_match.o passblk_mapper.o \
	passblk_input.o passblk_kfile.o passblk_term.o
