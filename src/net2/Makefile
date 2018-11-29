include ../rules.mk

$(eval $(call service,ifmon))
$(eval $(call command,ifctl))
$(eval $(call command,dhcp))

ifmon: ifmon.o ifmon_config.o ifmon_control.o ifmon_dhcp.o \
	ifmon_netlink.o ifmon_scripts.o ifmon_slots.o
