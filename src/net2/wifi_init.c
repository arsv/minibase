#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/dents.h>
#include <sys/ppoll.h>
#include <sys/sched.h>
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

static void contact_ifmon(CTX, int ifi, char* name)
{
	uc_put_hdr(UC, CMD_IF_SET_WIFI);
	uc_put_int(UC, ATTR_IFI, ifi);
	uc_put_end(UC);

	send_check(ctx);
}

/* There was a pretty big chunk of code here involving inotify.
   Turns out this problem is pretty difficult to solve correctly
   (taking in account sockmod) but it hardly ever matters.
   A simple timeout should be more than enough. */

static void wait_for_wictl(void)
{
	struct timespec ts = { .sec = 1, .nsec = 0 };
	sys_nanosleep(&ts, NULL);
}

/* Entry point for all the stuff above */

void try_start_wienc(CTX)
{
	char ifname[32+2];
	int ifi;

	find_wifi_device(ifname, sizeof(ifname));

	warn("trying to start wienc on", ifname, 0);

	ifi = resolve_iface(ctx, ifname);

	contact_ifmon(ctx, ifi, ifname);

	wait_for_wictl();
}
