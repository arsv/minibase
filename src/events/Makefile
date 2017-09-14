bin = inputs
sbin = acpid keymon lindevd

include ../rules.mk

keymon: keymon.o keymon_conf.o keymon_devs.o keymon_evts.o \
	keymon_mask.o keymon_keys.o

inputs: inputs.o inputs_list.o inputs_mask.o \
	inputs_name_key.o inputs_name_sw.o \
	inputs_name_abs.o inputs_name_rel.o \
	inputs_name_led.o inputs_read.o
