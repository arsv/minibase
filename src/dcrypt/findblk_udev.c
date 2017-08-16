#include <bits/socket/netlink.h>
#include <sys/pid.h>
#include <sys/sockio.h>
#include <sys/socket.h>

#include <string.h>
#include <fail.h>

#include "findblk.h"

static int udev;

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
		.groups = -1
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

static int any_missing_devs(void)
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

void wait_udev(void)
{
	while(any_missing_devs())
		recv_udev_event();
}
