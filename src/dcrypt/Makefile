include ../rules.mk

$(eval $(call sys,passblk))
$(eval $(call bin,dektool))
$(eval $(call bin,deitool))
$(eval $(call bin,dmcrypt))

dektool: dektool.o keytool.o
deitool: deitool.o keytool.o
dmcrypt: dmcrypt.o keytool.o

passblk: passblk.o passblk_input.o passblk_keyfile.o passblk_mapper.o
