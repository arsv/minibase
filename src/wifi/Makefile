/ = ../../

include $/config.mk

ifdef devel
CFLAGS += -DDEVEL
endif

bin = wiscan wimon wpa pbkdf

man1 =

include $/common.mk

wimon: wimon.o wimon_rtnl.o wimon_genl.o wimon_proc.o wimon_slot.o \
	wimon_genl_ap.o wimon_ctrl.o nlfam_grps.o wimon_link.o \
	wimon_save.o testap.o

wpa: wpa.o wpa_netlink.o wpa_eapol.o wpa_crypt.o nlfam_grps.o

pbkdf: pbkdf.o pbkdf2_sha1.o
