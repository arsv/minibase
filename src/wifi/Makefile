service = wimon
command = wictl wpa

include ../rules.mk

wimon: wimon.o wimon_rtnl.o wimon_genl.o wimon_proc.o wimon_slot.o \
	wimon_genl_ies.o wimon_ctrl.o wimon_ctrl_rep.o wimon_link.o \
	wimon_conf.o wimon_conf_buf.o wimon_wifi.o wimon_kill.o

wictl: wictl.o wictl_wire.o wictl_list.o wictl_pass.o

wpa: wpa.o wpa_netlink.o wpa_eapol.o wpa_crypt.o
