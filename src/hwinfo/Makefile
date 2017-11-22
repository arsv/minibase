include ../rules.mk

$(eval $(call command,lsusb,1))
$(eval $(call command,lspci))
$(eval $(call command,inputs,1))

inputs: inputs.o inputs_list.o inputs_mask.o \
	inputs_name_key.o inputs_name_sw.o \
	inputs_name_abs.o inputs_name_rel.o \
	inputs_name_led.o inputs_read.o
