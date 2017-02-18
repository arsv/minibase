/ = ../../

include $/config.mk

bin = scan wimon

man1 =

include $/common.mk

wimon: wimon.o wimon_rtnl.o wimon_genl.o wimon_poll.o \
	wimon_proc.o wimon_list.o wimon_scan.o wimon_ctrl.o
