#include <bits/errno.h>
#include <sys/file.h>
#include <sys/sched.h>

#include <errtag.h>
#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <heap.h>
#include <util.h>

#include "common.h"
#include "wifi.h"

/* Service startup */

static int is_wifi_device(char* name)
{
	struct stat st;

	FMTBUF(p, e, path, 70);
	p = fmtstr(p, e, "/sys/class/net/");
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, "/phy80211");
	FMTEND(p, e);

	return (sys_stat(path, &st) >= 0);
}

static int select_wifi_dev(struct ucmsg* msg, char* dev)
{
	struct ucattr* at;
	struct ucattr* ln;
	int *ifi, *flags, *mode;
	char* name;

	int selected = 0;
	char* selname = NULL;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at)) {
		if(!(ln = uc_is_nest(at, ATTR_LINK)))
			continue;
		if(!(ifi = uc_sub_int(ln, ATTR_IFI)))
			continue;
		if(!(flags = uc_sub_int(ln, ATTR_FLAGS)))
			continue;
		if(!(mode = uc_sub_int(ln, ATTR_MODE)))
			continue;
		if(!(name = uc_sub_str(ln, ATTR_NAME)))
			continue;

		if(dev && !strcmp(name, dev))
			return *ifi;

		if(*mode == IF_MODE_WIFI && !(*flags & IF_ERROR))
			return *ifi;
		if(*flags & IF_RUNNING)
			continue;
		if(!is_wifi_device(name))
			continue;

		if(!selected) {
			selected = *ifi;
			selname = name;
		} else {
			warn("multiple devices available", NULL, 0);
			warn(NULL, selname, 0);
			warn(NULL, name, 0);
			selname = NULL;
		}
	}

	if(!selected && dev)
		fail("device not found:", dev, 0);
	if(!selected)
		fail("no suitable devices found", NULL, 0);
	if(!selname)
		_exit(0xFF);

	warn("trying to start service on", selname, 0);

	return selected;
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

/* Ugly hack to be fixed later: the caller relies on the data
   in ctx->uc being preserved across this call. And it's not easy
   to save any other way. */

void try_start_wienc(CTX, char* dev)
{
	struct ucmsg* msg;
	char buf[128];

	struct ucbuf uc = {
		.brk = buf,
		.ptr = buf,
		.end = buf + sizeof(buf)
	}, old = ctx->uc;

	ctx->uc = uc;

	connect_ifctl(ctx);

	uc_put_hdr(UC, CMD_IF_STATUS);
	uc_put_end(UC);

	msg = send_recv_msg(ctx);

	int ifi = select_wifi_dev(msg, dev);

	uc_put_hdr(UC, CMD_IF_SET_WIFI);
	uc_put_int(UC, ATTR_IFI, ifi);
	uc_put_end(UC);

	send_check(ctx);

	wait_for_wictl();

	ctx->uc = old;
}
