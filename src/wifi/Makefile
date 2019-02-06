include ../rules.mk

$(eval $(call service,wsupp))
$(eval $(call command,wifi))

wsupp: wsupp.o wsupp_netlink.o wsupp_eapol.o wsupp_cntrl.o wsupp_slots.o \
	wsupp_apsel.o wsupp_rfkill.o wsupp_script.o wsupp_device.o

wifi: wifi.o wifi_aplist.o wifi_pskmgm.o wifi_device.o wifi_fmties.o
