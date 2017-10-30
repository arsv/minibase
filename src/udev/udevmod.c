#include <bits/socket/netlink.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/ioctl.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/socket.h>
#include <sys/signal.h>

#include <errtag.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"

ERRTAG("udevmod");

#define OPTS "sv"
#define OPT_s (1<<0)
#define OPT_v (1<<1)

#define UDEV_MGRP_KERNEL   (1<<0)

struct top {
	int udev;
	char** envp;
	int opts;

	int fd;  /* of a running modprobe -p process */
	int pid;
};

struct rfn {
	int at;
	char* dir;
	char* name;
};

#define CTX struct top* ctx
#define FN struct rfn* fn
#define AT(dd) dd->at, dd->name

/* During initial device scan, udevmod will try dozens of modaliases
   in quick succession, most of them invalid. There is no point in
   spawning that many modprobe processes. Instead, we spawn one and
   pipe it aliases to check.

   After the initial scan, this becomes pointless since udev events
   are rare and the ones with modaliases tend to arrive one at a time,
   so we switch to spawning modprobe on each event. */

static void open_modprobe(CTX)
{
	int fds[2];
	int ret, pid;

	if((ret = sys_pipe(fds)) < 0)
		fail("pipe", NULL, ret);
	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	char* arg = (ctx->opts & OPT_v) ? "-qbpv" : "-qbp";

	if(pid == 0) {
		char* argv[] = { "/sbin/modprobe", arg, NULL };
		sys_dup2(fds[0], STDIN);
		sys_close(fds[1]);
		ret = sys_execve(*argv, argv, ctx->envp);
		fail("execve", *argv, ret);
	}

	sys_close(fds[0]);

	ctx->pid = pid;
	ctx->fd = fds[1];
}

static void stop_modprobe(CTX)
{
	int pid = ctx->pid;
	int ret, status;
	int fd = ctx->fd;

	if(fd < 0) fd = -fd;

	sys_close(fd);

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		fail("waitpid", NULL, ret);

	ctx->fd = -1;
	ctx->pid = 0;
}

static void run_modprobe(CTX, char* name)
{
	int pid, ret, status;

	if((pid = sys_fork()) < 0) {
		warn("fork", NULL, pid);
		return;
	}

	if(pid == 0) {
		char* argv[] = { "/sbin/modprobe", "-qb", name, NULL };
		ret = sys_execve(*argv, argv, ctx->envp);
		fail("execve", *argv, ret);
	} else {
		if((ret = sys_waitpid(pid, &status, 0)) < 0)
			warn("waitpid", NULL, ret);
	}
}

static void out_modprobe(CTX, char* name)
{
	int fd = ctx->fd;
	int nlen = strlen(name);
	int ret;

	if(fd <= 0) return;

	name[nlen] = '\n';

	ret = sys_write(ctx->fd, name, nlen + 1);

	name[nlen] = '\0';

	if(ret == -EPIPE) ctx->fd = -ctx->fd;
}

static void modprobe(CTX, char* name)
{
	if(ctx->pid)
		out_modprobe(ctx, name);
	else
		run_modprobe(ctx, name);
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

static void suppress_sigpipe(void)
{
	struct sigaction sa = {
		.handler = SIG_IGN,
		.flags = 0,
		.restorer = NULL
	};

	sys_sigaction(SIGPIPE, &sa, NULL);
}

int main(int argc, char** argv, char** envp)
{
	struct top context, *ctx = &context;
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	ctx->envp = envp;
	ctx->opts = opts;

	open_udev(ctx);

	suppress_sigpipe();
	open_modprobe(ctx);
	scan_devices(ctx);
	stop_modprobe(ctx);

	if(opts & OPT_s) return 0;

	while(1) recv_event(ctx);
}
