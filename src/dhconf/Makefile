/=../../

all = dhconf

include ../rules.mk
include $/config.mk

dhconf: dhconf.o dhconf_packet.o dhconf_netlink.o dhconf_scripts.o \
	dhconf_options.o dhconf_leasout.o

-include *.d
