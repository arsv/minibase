/=../../

all = keymon acpid

include ../rules.mk
include $/config.mk

keymon: keymon.o keymon_conf.o keymon_devs.o keymon_evts.o \
	keymon_keys.o keymon_mask.o

-include *.d
