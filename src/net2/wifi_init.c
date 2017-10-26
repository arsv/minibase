#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/dents.h>
#include <sys/ppoll.h>
#include <sys/inotify.h>
#include <sys/socket.h>

#include <string.h>
#include <printf.h>
#include <format.h>
#include <nlusctl.h>
#include <heap.h>
#include <util.h>

#include "common.h"
#include "wifi.h"

static int is_nl80211_dev(int at, char* name)
{
	struct stat st;

	FMTBUF(p, e, path, 50);
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, "/phy80211");
	FMTEND(p, e);

	return (sys_fstatat(at, path, &st, 0) >= 0);
}

static void find_wifi_device(char* name, int nlen)
{
	char* dir = "/sys/class/net";
	char buf[1024];
	int fd, rd, got = 0;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;
			p += de->reclen;

			if(dotddot(de->name))
				continue;
			if(!is_nl80211_dev(fd, de->name))
				continue;
			if(strlen(de->name) > nlen - 1)
				fail(NULL, de->name, -ENAMETOOLONG);

			if(!got++) {
				int delen = strlen(de->name);
				memcpy(name, de->name, delen);
				name[delen] = '\0';
			} else if(got == 1) {
				warn("multiple devices available", NULL, 0);
				warn(NULL, name, 0);
				warn(NULL, de->name, 0);
			} else {
				warn(NULL, de->name, 0);
			}

		}
	}

	if(!got)
		fail("no wifi devices found", NULL, 0);
	else if(got > 1)
		_exit(0xFF);
}

static int resolve_iface(CTX, char* name)
{
	int ifi;

	if((ifi = getifindex(ctx->fd, name)) < 0)
		fail("cannot resolve iface name", name, 0);

	return ifi;
}

static int open_ifmon_socket(void)
{
	int fd, ret;

	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = IFCTL
	};

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_connect(fd, &addr, sizeof(addr))) < 0)
		fail("connect", addr.path, ret);

	return fd;
}

static void recv_ifmon_reply(int fd, char* name)
{
	char buf[64];
	int ret;

	struct urbuf ur = {
		.buf = buf,
		.mptr = buf,
		.rptr = buf,
		.end = buf + sizeof(buf)
	};

	while(1) {
		if((ret = uc_recv(fd, &ur, 1)) < 0)
			fail("connection lost", NULL, 0);

		struct ucmsg* msg = ur.msg;

		if(msg->cmd > 0)
			continue;
		if(msg->cmd < 0)
			fail(NULL, name, msg->cmd);

		return;
	}
}

static void send_ifmon_command(int fd, int ifi)
{
	char buf[64];
	int wr;

	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	};

	uc_put_hdr(&uc, CMD_IF_SET_WIFI);
	uc_put_int(&uc, ATTR_IFI, ifi);
	uc_put_end(&uc);

	int len = uc.ptr - uc.brk;

	if((wr = writeall(fd, buf, len)) < 0)
		fail("write", NULL, wr);
}

static void contact_ifmon(int ifi, char* name)
{
	int fd = open_ifmon_socket();

	send_ifmon_command(fd, ifi);
	recv_ifmon_reply(fd, name);

	sys_close(fd);
}

/* Wait until /var/run/wienc appears
   (a simplied version of `waitfor` msh builtin) */

static int check_file(char* path)
{
	struct stat st;
	return (sys_stat(path, &st) >= 0);
}

static int watch_ino_for(int fd, char* name, char* base)
{
	struct timespec ts = { .sec = 3, .nsec = 0 };
	struct pollfd pfd = { .fd = fd, .events = POLLIN };
	int ret, rd;

	while(1) {
		if((ret = sys_ppoll(&pfd, 1, &ts, NULL)) < 0)
			fail("ppoll", NULL, ret);
		if(ret == 0)
			fail("timeout waiting for", name, 0);

		char buf[512];
		int len = sizeof(buf);

		if((rd = sys_read(fd, buf, len)) < 0)
			fail("inotify", NULL, rd);

		char* end = buf + rd;
		char* ptr = buf;

		while(ptr < end) {
			struct inotify_event* ino = (void*) ptr;

			if(!strcmp(base, ino->name))
				return 0;

			ptr += sizeof(*ino) + ino->len;
		}
	}
}

static void prep_dirname(char* dir, int dlen, char* name, char* base)
{
	int len = base - name - 1;
	if(len > dlen - 1) len = dlen - 1;
	memcpy(dir, name, len);
	dir[len] = '\0';
}

static void wait_for_wictl(void)
{
	char* name = WICTL;
	char* base = basename(name);
	char dir[base - name + 3];
	long fd, wd;

	if((fd = sys_inotify_init1(IN_NONBLOCK)) < 0)
		fail("inotify", NULL, fd);

	prep_dirname(dir, sizeof(dir), name, base);

	if((wd = sys_inotify_add_watch(fd, dir, IN_CREATE)) < 0)
		fail(NULL, dir, wd);

	if(check_file(name))
		; /* the file already exists */
	else watch_ino_for(fd, name, base);

	sys_inotify_rm_watch(fd, wd);

	sys_close(fd);

	/* Lazy hack to let sockmod do its job. Monitoring for mode
	   bits change is actually quite difficult. */

	struct timespec ts = { .sec = 1, .nsec = 0 };
	sys_ppoll(NULL, 0, &ts, NULL);
}

/* Entry point for all the stuff above */

void try_start_wienc(CTX)
{
	char ifname[32+2];
	int ifi;

	find_wifi_device(ifname, sizeof(ifname));

	warn("trying to start wienc on", ifname, 0);

	ifi = resolve_iface(ctx, ifname);

	contact_ifmon(ifi, ifname);

	wait_for_wictl();
}
