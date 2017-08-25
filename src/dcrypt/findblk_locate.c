#include <bits/socket/netlink.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/dents.h>
#include <sys/fpath.h>
#include <sys/creds.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "findblk.h"

static int udev;

#define UDEV_MGRP_KERNEL   (1<<0)

/* Wait for udev events */

void open_udev(void)
{
	int fd, ret;

	int domain = PF_NETLINK;
	int type = SOCK_DGRAM;
	int proto = NETLINK_KOBJECT_UEVENT;

	if((fd = sys_socket(domain, type, proto)) < 0)
		fail("socket", "udev", fd);

	struct sockaddr_nl addr = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = UDEV_MGRP_KERNEL
	};

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", "udev", ret);

	udev = fd;
}

static char* restof(char* line, char* pref)
{
	int plen = strlen(pref);
	int llen = strlen(line);

	if(llen < plen)
		return NULL;
	if(strncmp(line, pref, plen))
		return NULL;

	return line + plen;
}

void recv_udev_event(void)
{
	int max = 1024;
	char buf[max+2];
	int rd;

	if((rd = sys_recv(udev, buf, max, 0)) < 0)
		fail("recv", "udev", rd);

	buf[rd] = '\0';

	char* p = buf;
	char* e = buf + rd;

	char* devtype = NULL;
	char* devname = NULL;
	char* r;

	if(strncmp(p, "add@", 4))
		return; /* ignore non-add events */

	while(p < e) {
		if((r = restof(p, "DEVTYPE=")))
			devtype = r;
		if((r = restof(p, "DEVNAME=")))
			devname = r;

		p += strlen(p) + 1;

		if(devtype && devname)
			break;
	}

	if(!devtype || !devname)
		return;

	if(!strcmp(devtype, "disk"))
		match_dev(devname);
	else if(!strcmp(devtype, "partition"))
		match_part(devname);
}

void wait_udev(void)
{
	while(any_missing_devs())
		recv_udev_event();
}

/* Scan for existing devices */

static void foreach_dir_in(char* dir, void (*func)(char*, char*), char* base);

static void check_part_ent(char* name, char* base)
{
	if(strncmp(name, base, strlen(base)))
		return;
	
	match_part(name);
}

static void check_dev_ent(char* name, char* _)
{
	if(!strncmp(name, "loop", 4))
		return;

	if(!match_dev(name))
		return;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/sys/block/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	foreach_dir_in(path, check_part_ent, name);
}

static inline int dotddot(const char* p)
{
	if(!p[0])
		return 1;
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

static void foreach_dir_in(char* dir, void (*func)(char*, char*), char* base)
{
	int len = 1024;
	char buf[len];
	long fd, rd;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail("open", dir, fd);

	while((rd = sys_getdents(fd, buf, len)) > 0) {
		char* ptr = buf;
		char* end = buf + rd;
		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			switch(de->type) {
				case DT_UNKNOWN:
				case DT_DIR:
				case DT_LNK:
					break;
				default:
					continue;
			}

			func(de->name, base);
		}
	} if(rd < 0) {
		fail("getdents", dir, rd);
	}

	sys_close(fd);
}

void scan_devs(void)
{
	foreach_dir_in("/sys/block", check_dev_ent, NULL);
}

/* Link simple (non-encrypted) partitions */

static void link_part(char* name, char* label)
{
	FMTBUF(lp, le, link, 100);
	lp = fmtstr(lp, le, MAPDIR);
	lp = fmtstr(lp, le, "/");
	lp = fmtstr(lp, le, label);
	FMTEND(lp, le);

	FMTBUF(pp, pe, path, 100);
	pp = fmtstr(pp, pe, "/dev/");
	pp = fmtstr(pp, pe, name);
	FMTEND(pp, pe);

	sys_symlink(path, link);
}

void link_parts(void)
{
	struct part* pt;

	sys_mkdir(MAPDIR, 0755);

	for(pt = parts; pt < parts + nparts; pt++)
		link_part(pt->name, pt->label);
}

/* Check current state */

int any_missing_devs(void)
{
	struct bdev* bd;
	struct part* pt;
	
	for(bd = bdevs; bd < bdevs + nbdevs; bd++)
		if(!bd->here)
			return 1;

	for(pt = parts; pt < parts + nparts; pt++)
		if(!pt->here)
			return 1;

	return 0;
}

int any_encrypted_parts(void)
{
	struct part* pt;

	for(pt = parts; pt < parts + nparts; pt++)
		if(pt->keyidx)
			return 1;

	return 0;
}

