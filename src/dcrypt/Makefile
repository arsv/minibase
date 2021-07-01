/=../../

all = passblk dektool deitool dmcrypt

include $/config.mk
include ../rules.mk

dektool: dektool.o keytool.o

deitool: deitool.o keytool.o

dmcrypt: dmcrypt.o keytool.o

passblk: passblk.o passblk_input.o passblk_keyfile.o passblk_mapper.o

-include *.d
