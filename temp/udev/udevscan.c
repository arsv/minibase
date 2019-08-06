#include <bits/socket/netlink.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/ppoll.h>
#include <sys/creds.h>
#include <sys/ioctl.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/mman.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>
#include <main.h>

ERRTAG("udevscan");

/* Quick sketch for an event-triggering udev device scan.
   Also doubles as a tool to listing possible subsystem-devtype values.

   This was meant to be a part of udevmod, but the idea has been scraped
   for now. */

struct top {
	int fd;
	char** envp;
	char uevent[1024+2];
	int len;
};

#define CTX struct top* ctx __unused

#define UDEV_MGRP_KERNEL   (1<<0)

char* get_val(CTX, char* key)
{
	char* p = ctx->uevent;
	char* q;
	char* e = p + ctx->len;

	int klen = strlen(key);

	while(p < e) {
		if(!(q = strchr(p, '=')))
			goto next;
		if(q - p < klen)
			goto next;

		if(!strncmp(p, key, klen))
			return q + 1;

		next: p += strlen(p) + 1;
	}

	return NULL;
}

/* Generally udev events either carry a MODALIAS (unknown device, asking
   for a module) or have DEVNAME (device has been picked up by a module).
   Apparently it cannot be both at the same time. There are however events
   without MODALIAS and without DEVNAME. */

static void dev_added(CTX)
{
	char *devname, *subsystem, *devtype;

	if(!(subsystem = get_val(ctx, "SUBSYSTEM")))
		return;
	if(!(devname = get_val(ctx, "DEVNAME")))
		return;

	if((devtype = get_val(ctx, "DEVTYPE")))
		tracef("ADD %s-%s %s\n", subsystem, devtype, devname);
	else
		tracef("ADD %s %s\n", subsystem, devname);
}

static void recv_event(CTX)
{
	int rd, fd = ctx->fd;
	int max = sizeof(ctx->uevent) - 2;
	char* buf = ctx->uevent;

	if((rd = sys_recv(fd, buf, max, 0)) < 0)
		fail("recv", "udev", rd);

	buf[rd] = '\0';
	ctx->len = rd;

	if(strncmp(buf, "add@", 4))
		return;

	dev_added(ctx);
}

static void open_udev(CTX)
{
	int fd, ret;
	int pid = sys_getpid();

	int domain = PF_NETLINK;
	int type = SOCK_DGRAM;
	int proto = NETLINK_KOBJECT_UEVENT;

	if((fd = sys_socket(domain, type, proto)) < 0)
		fail("socket", "udev", fd);

	struct sockaddr_nl addr = {
		.family = AF_NETLINK,
		.pid = pid,
		.groups = UDEV_MGRP_KERNEL
	};

	if((ret = sys_bind(fd, &addr, sizeof(addr))) < 0)
		fail("bind", "udev", ret);

	ctx->fd = fd;
}

static void trigger(CTX, int at)
{
	int fd;

	if((fd = sys_openat(at, "uevent", O_WRONLY)) < 0)
		return;

	(void)sys_write(fd, "add\n", 4);
}

static void scan_dir(CTX, int at, char* name)
{
	int len = 1024;
	char buf[len];
	int fd, rd;

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		return;

	trigger(ctx, fd);

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
			if(de->type != DT_DIR)
				continue;

			scan_dir(ctx, fd, de->name);
		}
	}

	sys_close(fd);
}

static void fork_scan(CTX)
{
	int pid;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0) {
		sys_close(ctx->fd);
		scan_dir(ctx, AT_FDCWD, "/sys/devices");
		_exit(0);
	}
}

static void setup_signals(CTX)
{
	int ret;
	SIGHANDLER(sa, SIG_IGN, 0);

	if((ret = sys_sigaction(SIGCHLD, &sa, NULL)) < 0)
		fail("sigactin", "SIGCHLD", ret);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	setup_signals(ctx);

	open_udev(ctx);
	fork_scan(ctx);

	while(1) recv_event(ctx);
}
