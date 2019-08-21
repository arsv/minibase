include ../rules.mk

$(eval $(call service,ifmon))
$(eval $(call command,ifctl))

ifmon: ifmon.o ifmon_actions.o ifmon_control.o ifmon_netlink.o \
	ifmon_scripts.o ifmon_slots.o

ifctl: ifctl.o ifctl_config.o ifctl_status.o
