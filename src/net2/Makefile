service = ifmon
network = wienc dhcp
command = wifi ipcfg ipinfo ifctl

include ../rules.mk

wienc: wienc.o wienc_netlink.o wienc_eapol.o wienc_crypto.o wienc_cntrl.o \
	wienc_slots.o wienc_sta_ies.o wienc_config.o wienc_apsel.o \
	wienc_rfkill.o wienc_ifmon.o

wifi: wifi.o wifi_dump.o wifi_pass.o wifi_wire.o wifi_init.o

ifmon: ifmon.o ifmon_ctrl.o ifmon_rtnl.o ifmon_exec.o ifmon_slot.o \
	ifmon_link.o ifmon_conf.o

dhcp: dhcp.o dhcp_dump.o dhcp_conf.o dhcp_udp.o
