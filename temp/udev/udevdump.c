#include <bits/socket/netlink.h>

#include <sys/file.h>
#include <sys/creds.h>
#include <sys/socket.h>

#include <errtag.h>
#include <string.h>
#include <util.h>

/* Simple udev event dumper, for figuring out how the messages look like. */

#define UDEV_MGRP_KERNEL   (1<<0)
#define UDEV_MGRP_LIBUDEV  (1<<1) /* we don't dump these */

ERRTAG("udevdump");

/* UDEV events arrive one at a time. Kernel-generated udev messages are
   simple chunks of 0-terminated strings, each string except the first
   being VAR=val. */

static void dump(char* buf, int len)
{
	char* p;

	for(p = buf; p < buf + len; p++)
		if(!*p) *p = '\n';

	*p++ = '\n';

	sys_write(STDOUT, buf, p - buf);
}

static int open_udev(int groups)
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
		.groups = groups
	};

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", "udev", ret);

	return fd;
}

int main(int argc, char** argv)
{
	(void)argv;
	int fd, rd;
	char buf[2048];
	int max = sizeof(buf) - 2;
	int groups;

	if(argc < 2)
		groups = UDEV_MGRP_KERNEL;
	else if(!strcmp(argv[1], "user"))
		groups = UDEV_MGRP_LIBUDEV;
	else if(!strcmp(argv[1], "both"))
		groups = UDEV_MGRP_KERNEL | UDEV_MGRP_LIBUDEV;
	else
		fail("bad group spec", argv[1], 0);

	if(argc > 2)
		fail("too many arguments", NULL, 0);

	fd = open_udev(groups);

	while((rd = sys_recv(fd, buf, max, 0)) > 0)
		dump(buf, rd);
	if(rd < 0)
		fail("recv", "udev", rd);

	return 0;
}
