#include <bits/ints.h>
#include <bits/rfkill.h>
#include <sys/open.h>
#include <sys/stat.h>
#include <sys/read.h>

#include <null.h>
#include <format.h>

#include "wimon.h"

/* RFkill handling in Linux is weird, to put things mildly.

   When a card gets rf-killed, the link loses IFF_UP and RTNL gets notification
   of state change. But when rfkill gets undone, the reverse does not happen.
   The interface remains in "down" state and must be commanded back "up".
   RTNL layer also gets no notifications of any kind that rf-unkill happened.

   The only somewhat reliable way to be notified is by listening to /dev/rfkill.
   Now that device however is provided by a standalone module that may not be
   loadeded at any given time, and may get un-/re-loaded. Normally this does not
   happens, so wimon keeps an open fd. However if open attempt fails, wimon
   keeps trying to re-open it on any suitable occasion. This leads to redundant
   open calls in case rfkill is really missing, but there's probably no other
   way around this.

   Another problem is that /dev/rfkill reports events for rfkill devices (idx
   in the struct below) which do *not* match netdev ifi-s. The trick used here
   is to check /sys/class/net/$ifname/phy80211/rfkill$idx whenever any relevant
   event arrives. The $idx-$ifname association seems to be stable for at least
   as long as fd remains opens, but there are no guarantees beyond that.
   Hopefully rfkill events are rare.

   The end result of this all is effectively "ifconfig (iface) up" being run
   each time some managed link gets un-killed. Link state change results in RTNL
   event which triggers link_enabled which in turn proceeds to restore wifi
   connection if necessary. */

int rfkillfd;

static int match_rfkill_link(struct link* ls, int idx)
{
	char path[256];
	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, "/sys/class/net/");
	p = fmtstr(p, e, ls->name);
	p = fmtstr(p, e, "/phy80211/rfkill");
	p = fmtint(p, e, idx);
	*p++ = '\0';

	struct stat st;

	return (sysstat(path, &st) >= 0);
}

static struct link* find_rfkill_link(int idx)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi || !(ls->flags & S_NL80211))
			continue;
		if(ls->flags & S_ENABLED)
			continue;

		if(ls->rfk > 0) {
			if(ls->rfk == idx)
				return ls;
			else
				continue;
		} else if(match_rfkill_link(ls, idx)) {
			ls->rfk = idx;
			return ls;
		}
	}

	return NULL;
}

static void handle_event(struct rfkill_event* re)
{
	struct link* ls;

	if(re->soft || re->hard)
		return;
	if(!(ls = find_rfkill_link(re->idx)))
		return;

	enable_iface(ls->ifi);
}

void retry_rfkill(void)
{
	if(rfkillfd > 0)
		return;

	rfkillfd = sysopen("/dev/rfkill", O_RDONLY | O_NONBLOCK);

	update_killfd();
}

void reset_rfkill(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		ls->rfk = 0;

	rfkillfd = 0;
}

/* One event per read() here, even if more are queued. */

void handle_rfkill(void)
{
	char buf[128];
	struct rfkill_event* re;
	int fd = rfkillfd;
	int rd;

	while((rd = sysread(fd, buf, sizeof(buf))) > 0) {
		re = (struct rfkill_event*) buf;

		if(rd < sizeof(*re))
			continue;
		if(re->type != RFKILL_TYPE_WLAN)
			continue;

		handle_event(re);
	}
}
