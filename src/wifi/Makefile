include ../rules.mk

$(eval $(call service,wsupp))
$(eval $(call command,wifi))

wsupp: wsupp.o wsupp_netlink.o wsupp_eapol.o wsupp_crypto.o wsupp_cntrl.o \
	wsupp_slots.o wsupp_sta_ies.o wsupp_config.o wsupp_apsel.o \
	wsupp_rfkill.o wsupp_ifmon.o

wifi: wifi.o wifi_dump.o wifi_pass.o wifi_wire.o wifi_init.o
