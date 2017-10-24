service = wienc wifi ipcfg ipinfo nimon dhcp

include ../rules.mk

wienc: wienc.o wienc_netlink.o wienc_eapol.o wienc_crypto.o wienc_cntrl.o \
	wienc_slots.o wienc_sta_ies.o wienc_config.o wienc_apsel.o \
	wienc_rfkill.o

wifi: wifi.o wifi_dump.o wifi_pass.o wifi_wire.o

nimon: nimon.o nimon_ctrl.o nimon_rtnl.o nimon_exec.o nimon_slot.o \
	nimon_link.o nimon_conf.o

dhcp: dhcp.o dhcp_dump.o dhcp_conf.o dhcp_udp.o
