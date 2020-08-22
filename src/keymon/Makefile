include ../rules.mk

$(eval $(call sys,keymon,8))
$(eval $(call sys,acpid,8))

keymon: keymon.o keymon_conf.o keymon_devs.o keymon_evts.o \
	keymon_keys.o keymon_mask.o
