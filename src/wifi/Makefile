/ = ../../

include $/config.mk

bin = wiscan wimon wpa

man1 =

include $/common.mk

wimon: wimon.o wimon_rtnl.o wimon_genl.o wimon_proc.o wimon_slots.o \
	wimon_genl_sta.o wimon_ctrl.o nlfam_grps.o

wpa: wpa.o wpa_radio.o wpa_eapol.o wpa_crypt.o nlfam_grps.o
