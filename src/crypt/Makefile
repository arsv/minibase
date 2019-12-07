include ../rules.mk

$(eval $(call system,passblk,8))
$(eval $(call command,dektool,8))
$(eval $(call command,deitool,8))
$(eval $(call command,dmcrypt,8))

dektool: dektool.o keytool.o
deitool: deitool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

passblk: passblk.o passblk_input.o passblk_keyfile.o passblk_mapper.o
