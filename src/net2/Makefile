include ../rules.mk

$(eval $(call service,ifmon))
$(eval $(call command,ifctl))
$(eval $(call other,stubhcp))

ifmon: ifmon.o ifmon_control.o ifmon_netlink.o ifmon_scripts.o

ifctl: ifctl.o ifctl_config.o ifctl_status.o
