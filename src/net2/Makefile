include ../rules.mk

$(eval $(call service,ifmon))
$(eval $(call command,ifctl))

$(eval $(call service,wsupp))
$(eval $(call command,wifi))

$(eval $(call command,ipcfg))
$(eval $(call command,ipinfo))

wsupp: wsupp.o wsupp_netlink.o wsupp_eapol.o wsupp_crypto.o wsupp_cntrl.o \
	wsupp_slots.o wsupp_sta_ies.o wsupp_config.o wsupp_apsel.o \
	wsupp_rfkill.o wsupp_ifmon.o

wifi: wifi.o wifi_dump.o wifi_pass.o wifi_wire.o wifi_init.o

ifmon: ifmon.o ifmon_ctrl.o ifmon_rtnl.o ifmon_exec.o \
	ifmon_slot.o ifmon_link.o ifmon_conf.o \
	ifmon_dhcp.o ifmon_dhcp_recv.o ifmon_dhcp_send.o
