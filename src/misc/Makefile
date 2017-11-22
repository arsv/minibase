include ../rules.mk

$(eval $(call service,keymon,8))
$(eval $(call service,sockmod,8))
$(eval $(call service,acpid,8))
$(eval $(call service,rootsh))

keymon: keymon.o keymon_conf.o keymon_devs.o keymon_evts.o \
	keymon_keys.o keymon_mask.o
