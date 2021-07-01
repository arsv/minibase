/=../../

all = ifmon ifctl
aux = z-dhconf

include ../rules.mk
include $/config.mk

ifmon: ifmon.o ifmon_control.o ifmon_netlink.o ifmon_scripts.o

ifctl: ifctl.o ifctl_config.o ifctl_status.o

-include *.d
