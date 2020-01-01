include ../rules.mk

$(eval $(call sys,wsupp))
$(eval $(call bin,wifi))

wsupp: wsupp.o wsupp_eapol.o wsupp_control.o wsupp_slots.o \
	wsupp_apsel.o wsupp_bssid.o wsupp_script.o wsupp_netlink.o \
	wsupp_nlinit.o wsupp_nlauth.o wsupp_nlscan.o wsupp_nlkeys.o

wifi: wifi.o wifi_aplist.o wifi_pskmgm.o wifi_device.o \
	wifi_fmtaps.o wifi_fmties.o
