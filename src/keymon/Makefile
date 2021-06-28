include ../rules.mk

$(eval $(call sys,keymon))
$(eval $(call sys,acpid))

keymon: keymon.o keymon_conf.o keymon_devs.o keymon_evts.o \
	keymon_keys.o keymon_mask.o
