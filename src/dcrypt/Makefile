include ../rules.mk

$(eval $(call sys,passblk,8))
$(eval $(call bin,dektool,8))
$(eval $(call bin,deitool,8))
$(eval $(call bin,dmcrypt,8))

dektool: dektool.o keytool.o
deitool: deitool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

passblk: passblk.o passblk_input.o passblk_keyfile.o passblk_mapper.o
