#include <bits/rfkill.h>
#include <sys/file.h>

#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "wsupp.h"

/* When a card gets rf-killed, the link loses IFF_UP and RTNL gets notification
   of a state change. But when rfkill gets undone, the reverse does not happen.
   The interface remains in "down" state and must be commanded back "up".
   RTNL layer also gets no notifications of any kind that rf-unkill happened
   (and this tool doesn't even listen to RTNL notifications).

   The only somewhat reliable way to be notified is by listening to /dev/rfkill.
   But rfkill device is provided by a standalone module that may not be loadeded
   at any given time, and may get un-/re-loaded. Normally this does not happens,
   so wimon keeps the fd open. However if open attempt fails, wsupp will try to
   re-open it on any suitable occasion. This may lead to redundant open calls
   in case rfkill is in fact missing, but there's probably no other way around
   this. Hopefully events that trigger reopen attempts are rare.

   Another problem is that /dev/rfkill reports events for rfkill devices (idx
   in the struct below) which do *not* match netdev ifi-s. The trick used here
   is to check for /sys/class/net/$ifname/phy80211/rfkill$idx. The $idx-$ifname
   association seems to be stable for at least as long as the fd remains open,
   but there are no guarantees beyond that.

   The end result of this all is effectively `ifconfig (iface) up` done each
   time some managed link gets un-killed, followed by attempt to re-establish
   connection. */

int rfkill;
int rfkilled;
static int rfkidx;

/* The interface gets brought up with simple ioctls. It could have been done
   with RTNL as well, but setting up RTNL for this mere reason hardly makes
   sense.

   Current code works on interface named $ifname, in contrast with GENL code
   that uses $ifindex instead. Renaming the interface while wsupp runs *will*
   confuse it. */

static int match_rfkill(int idx)
{
	struct stat st;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/sys/class/net/");
	p = fmtstr(p, e, ifname);
	p = fmtstr(p, e, "/phy80211/rfkill");
	p = fmtint(p, e, idx);
	FMTEND(p, e);

	return (sys_stat(path, &st) >= 0);
}

static void handle_event(struct rfkill_event* re)
{
	if(rfkidx < 0) {
		if(match_rfkill(re->idx))
			rfkidx = re->idx;
		else
			return;
	} else if(re->idx != rfkidx) {
		return;
	}

	if(re->soft || re->hard) {
		rfkilled = 1;
		printf("rf-killed\n");
		clr_timer();
	} else {
		rfkilled = 0;
		printf("rf-restored\n");

		if((bring_iface_up()) < 0)
			return;

		handle_rfrestored();
	}
}

void retry_rfkill(void)
{
	if(rfkill > 0)
		return;

	rfkill = sys_open("/dev/rfkill", O_RDONLY | O_NONBLOCK);

	rfkidx = -1;
	pollset = 0;
}

void close_rfkill(void)
{
	if(rfkill < 0)
		return;

	sys_close(rfkill);

	rfkill = -1;
	rfkidx = -1;
	pollset = 0;
}

/* One event per read() here, even if more are queued. */

void handle_rfkill(void)
{
	char buf[128];
	struct rfkill_event* re;
	int fd = rfkill;
	int rd;

	while((rd = sys_read(fd, buf, sizeof(buf))) > 0) {
		re = (struct rfkill_event*) buf;

		if((ulong)rd < sizeof(*re))
			continue;
		if(re->type != RFKILL_TYPE_WLAN)
			continue;

		handle_event(re);
	}
}
