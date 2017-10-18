service = wienc wifi

include ../rules.mk

wienc: wienc.o wienc_netlink.o wienc_eapol.o wienc_crypto.o wienc_cntrl.o \
	wienc_slots.o wienc_sta_ies.o wienc_config.o wienc_apsel.o \
	wienc_rfkill.o

wifi: wifi.o wifi_list.o wifi_pass.o wifi_wire.o
