/ = ../

include $/config.mk

bin = cat chmod chown chvt clear df dmesg du echo errno \
	false hello hexdump kill mkdir mknod ren del sleep strings sync \
	sysinfo tee touch true uname uptime wc which write xargs yes symlink \
	stat ls whoami fn bcp date ff time waitfor

sbin = fstrim mount mountvfs switchroot systime

man1 = cat.1 chmod.1 chown.1 clear.1 df.1 du.1 echo.1 errno.1 hexdump.1 \
	kill.1 mkdir.1 ren.1 del.1 sleep.1 strings.1 symlink.1 \
	sync.1 tee.1 touch.1 uname.1 wc.1 which.1 write.1 xargs.1 yes.1 \
	stat.1 ls.1 fn.1 bcp.1 ff.1 time.1 waitfor.1

man8 = dmesg.8 fstrim.8 mount.8 mountvfs.8 switchroot.8 \
	sysinfo.8 systime.8 uptime.8 

include $/common.mk

date: date.o date_find.o date_time.o
