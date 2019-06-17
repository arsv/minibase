#include <bits/rfkill.h>
#include <bits/ioctl/socket.h>
#include <sys/ioctl.h>
#include <sys/file.h>

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
   so wsupp keeps the fd open. However if open attempt fails, wsupp will try to
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

#define IFF_UP (1<<0)

int rfkill;
int rfkilled;
static int rfkidx;

int bring_iface_up(void)
{
	int ret, fd = netlink;
	char* name = ifname;
	uint nlen = strlen(ifname);
	struct ifreq ifr;

	if(nlen > sizeof(ifr.name))
		return -ENAMETOOLONG;

	memzero(&ifr, sizeof(ifr));
	memcpy(ifr.name, name, nlen);

	if((ret = sys_ioctl(fd, SIOCGIFFLAGS, &ifr)) < 0) {
		warn("ioctl SIOCGIFFLAGS", name, ret);
		return ret;
	}

	if(ifr.ival & IFF_UP)
		return 0;

	ifr.ival |= IFF_UP;

	if((ret = sys_ioctl(fd, SIOCSIFFLAGS, &ifr)) < 0) {
		warn("ioctl SIOCSIFFLAGS", name, ret);
		return ret;
	}

	return 0;
}

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
	if(rfkidx >= 0) {
		if(re->idx != rfkidx)
			return;
	} else { /* not set yet */
		if(!match_rfkill(re->idx))
			return;
		rfkidx = re->idx;
	}

	if(re->soft || re->hard) {
		rfkilled = 1;
		clr_timer();
	} else {
		rfkilled = 0;

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
