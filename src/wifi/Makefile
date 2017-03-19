/ = ../../

include $/config.mk

bin = wiscan wpa pbkdf

man1 =

include $/common.mk

wpa: wpa.o wpa_netlink.o wpa_eapol.o wpa_crypt.o nlfam_grps.o

pbkdf: pbkdf.o pbkdf2_sha1.o
