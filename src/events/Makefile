/ = ../../

include $/config.mk

sbin = acpid keymon

man8 = acpid.8 keymon.8

include $/common.mk

keymon: keymon.o keymon_conf.o keymon_devs.o keymon_evts.o keymon_mask.o keymon_keys.o
