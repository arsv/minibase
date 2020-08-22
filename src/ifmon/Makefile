include ../rules.mk

$(eval $(call sys,ifmon))
$(eval $(call bin,ifctl))

$(eval $(call skip,z-dhconf))

ifmon: ifmon.o ifmon_control.o ifmon_netlink.o ifmon_scripts.o

ifctl: ifctl.o ifctl_config.o ifctl_status.o
