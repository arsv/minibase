/ = ../../

include $/config.mk

bin = bcp cat chmod chown clear date del df dmesg du errno ff fn hexdump \
	ls ren stat strings symlink sysinfo time which whoami

man1 = bcp.1 cat.1 chmod.1 chown.1 clear.1 date.1 del.1 df.1 du.1 errno.1 \
	ff.1 fn.1 hexdump.1 ls.1 ren.1 stat.1 strings.1 symlink.1 time.1 \
	which.1

include $/common.mk

date: date.o date_find.o date_time.o
