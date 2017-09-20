#include <bits/socket/netlink.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/ioctl.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/socket.h>

#include <errtag.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"

ERRTAG("udevmod");

#define UDEV_MGRP_KERNEL   (1<<0)

struct top {
	int udev;
	char** envp;
};

struct rfn {
	int at;
	char* dir;
	char* name;
};

#define CTX struct top* ctx
#define FN struct rfn* fn
#define AT(dd) dd->at, dd->name

static void modprobe(CTX, char* name)
{
	int pid, ret, status;

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	}

	if(pid == 0) {
		char* argv[] = { "/sbin/modprobe", "-q", name, NULL };
		ret = sys_execve(*argv, argv, ctx->envp);
		fail("execve", *argv, ret);
	} else {
		if((ret = sys_waitpid(pid, &status, 0)) < 0)
			warn("waitpid", NULL, ret);
	}
}

static char* get_modalias(char* buf, int len)
{
	char* end = buf + len;
	char* p = buf;
	char* q;

	while(p < end) {
		if(!(q = strchr(p, '=')))
			goto next;
		*q++ = '\0';

		if(!strcmp(p, "MODALIAS"))
			return q;

		next: p += strlen(p) + 1;
	}

	return NULL;
}

static void recv_event(CTX)
{
	int fd = ctx->udev;
	int rd, max = 1024;
	char buf[max+2];
	char* alias;

	if((rd = sys_recv(fd, buf, max, 0)) < 0)
		fail("recv", "udev", rd);

	buf[rd] = '\0';

	if(strncmp(buf, "add@", 4))
		return;

	if(!(alias = get_modalias(buf, rd)))
		return;

	modprobe(ctx, alias);
}

static void open_udev(CTX)
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

	ctx->udev = fd;
}

static int pathlen(FN)
{
	int len = strlen(fn->name);

	if(fn->dir && fn->name[0] != '/')
		len += strlen(fn->dir) + 1;

	return len + 1;
}

static void makepath(char* buf, int len, FN)
{
	char* p = buf;
	char* e = buf + len - 1;

	if(fn->dir && fn->name[0] != '/') {
		p = fmtstr(p, e, fn->dir);
		p = fmtstr(p, e, "/");
	};

	p = fmtstr(p, e, fn->name);
	*p = '\0';
}

static void pick_file(CTX, FN)
{
	int fd, rd;
	char buf[100];
	int len = sizeof(buf) - 1;

	if((fd = sys_openat(AT(fn), O_RDONLY)) < 0)
		return;

	if((rd = sys_read(fd, buf, len)) > 0) {
		if(buf[rd-1] == '\n')
			buf[rd-1] = '\0';
		else
			buf[rd] = '\0';

		modprobe(ctx, buf);
	}

	sys_close(fd);
}

static void scan_dir(CTX, FN)
{
	int len = 1024;
	char buf[len];
	int fd, rd;

	if((fd = sys_openat(AT(fn), O_DIRECTORY)) < 0)
		return;

	char path[pathlen(fn)];
	makepath(path, sizeof(path), fn);
	struct rfn next = { fd, path, NULL };

	while((rd = sys_getdents(fd, buf, len)) > 0) {
		char* ptr = buf;
		char* end = buf + rd;
		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!de->reclen)
				break;

			ptr += de->reclen;
			next.name = de->name;

			if(dotddot(de->name))
				continue;
			if(de->type == DT_DIR)
				scan_dir(ctx, &next);
			else if(de->type != DT_REG)
				continue;
			if(!strcmp(de->name, "modalias"))
				pick_file(ctx, &next);
		}
	}

	sys_close(fd);
}

static void scan_devices(CTX)
{
	struct rfn start = { AT_FDCWD, NULL, "/sys/devices" };

	scan_dir(ctx, &start);
}

int main(int argc, char** argv, char** envp)
{
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	ctx->envp = envp;

	open_udev(ctx);
	scan_devices(ctx);

	while(1) recv_event(ctx);
}
