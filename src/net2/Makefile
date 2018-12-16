include ../rules.mk

$(eval $(call service,ifmon))
$(eval $(call command,ifctl))
$(eval $(call command,dhcp))

ifmon: ifmon.o ifmon_control.o ifmon_netlink.o ifmon_scripts.o ifmon_slots.o

ifctl: ifctl.o ifctl_config.o ifctl_status.o

dhcp: dhcp.o dhcp_netlink.o dhcp_lease.o dhcp_show.o dhcp_wire.o dhcp_scripts.o
