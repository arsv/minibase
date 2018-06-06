include ../rules.mk

$(eval $(call service,ifmon))
$(eval $(call command,ifctl))

$(eval $(call command,ipcfg))
$(eval $(call command,ipinfo))

ifmon: ifmon.o ifmon_ctrl.o ifmon_rtnl.o ifmon_exec.o \
	ifmon_slot.o ifmon_link.o ifmon_conf.o \
	ifmon_dhcp.o ifmon_dhcp_recv.o ifmon_dhcp_send.o
