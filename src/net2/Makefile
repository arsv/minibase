service = ifmon
network = wsupp dhcp
command = wifi ipcfg ipinfo ifctl

include ../rules.mk

wsupp: wsupp.o wsupp_netlink.o wsupp_eapol.o wsupp_crypto.o wsupp_cntrl.o \
	wsupp_slots.o wsupp_sta_ies.o wsupp_config.o wsupp_apsel.o \
	wsupp_rfkill.o wsupp_ifmon.o

wifi: wifi.o wifi_dump.o wifi_pass.o wifi_wire.o wifi_init.o

ifmon: ifmon.o ifmon_ctrl.o ifmon_rtnl.o ifmon_exec.o ifmon_slot.o \
	ifmon_link.o ifmon_conf.o ifmon_dhcp.o

dhcp: dhcp.o dhcp_dump.o dhcp_conf.o dhcp_udp.o
