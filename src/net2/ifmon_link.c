#include <sys/time.h>

#include <util.h>
#include "ifmon.h"

static void start_wsupp(LS)
{
	char* argv[] = { "wsupp", ls->name, NULL };

	if(spawn(ls, CH_WIENC, argv) < 0)
		return;

	ls->flags |= LF_RUNNING;
}

void start_dhcp(LS)
{
	char* argv[] = { "dhcp", ls->name, NULL };

	if(spawn(ls, CH_DHCP, argv) < 0) {
		ls->flags |= LF_DHCPREQ;
	} else {
		ls->flags &= ~(LF_DHCPREQ | LF_DHCPFAIL);
		ls->flags |= (LF_RUNNING | LF_ADDRSET);
	}
}

static void stop_dhcp(LS)
{
	kill_tagged(ls, CH_DHCP);
}

static void flush_link(LS)
{
	if(ls->flags & LF_FLUSHING)
		return;

	ls->lease = 0;
	ls->flags &= ~LF_FLUSHREQ;
	ls->flags |= LF_FLUSHING;
	delete_addr(ls);
}

static void dhcp_exit(LS, int status)
{
	if(status)
		ls->flags |= LF_DHCPFAIL;

	report_link_dhcp(ls, status);

	if(ls->flags & LF_FLUSHREQ)
		return flush_link(ls);
	if(ls->flags & LF_DHCPREQ)
		return start_dhcp(ls);
}

static void wsupp_exit(LS, int status)
{
	if(ls->flags & LF_STOP)
		return;
	if(ls->mode != LM_WIFI)
		return;

	if(status) {
		ls->flags |= LF_ERROR;
		stop_link(ls);
	} else {
		start_wsupp(ls);
	}
}

static void maybe_mark_stopped(LS)
{
	if(!(ls->flags & LF_STOP))
		return;
	if(!(ls->flags & LF_RUNNING))
		return;
	if(any_procs_left(ls))
		return;

	ls->flags &= ~LF_RUNNING;

	report_link_stopped(ls);
}

int stop_link(LS)
{
	if(!(ls->flags & LF_RUNNING))
		return -EALREADY;
	if(ls->flags & LF_STOP)
		return 0;

	ls->flags |= LF_STOP;
	stop_link_procs(ls, 0);
	flush_link(ls);
	maybe_mark_stopped(ls);

	return 0;
}

void link_new(LS)
{
	load_link(ls);

	int isup = (ls->flags & LF_ENABLED);
	int mode = ls->mode;

	if(mode == LM_SKIP)
		return;
	if(mode == LM_DOWN) {
		if(isup) disable_iface(ls);
	} else {
		if(!isup) enable_iface(ls);
	}
}

static int effmode(LS, int mode)
{
	if(ls->flags & (LF_STOP | LF_ERROR))
		return 0;

	return (ls->mode == mode);
}

void link_enabled(LS)
{
	if(effmode(ls, LM_WIFI))
		start_wsupp(ls);

	report_link_enabled(ls);
}

void link_carrier(LS)
{
	if(effmode(ls, LM_DHCP))
		start_dhcp(ls);

	report_link_carrier(ls);
}

void link_lost(LS)
{
	ls->flags &= ~LF_DHCPREQ;

	if(ls->flags & LF_ADDRSET)
		flush_link(ls);
}

void link_down(LS)
{
	ls->flags &= ~(LF_ADDRSET | LF_FLUSHREQ);

	stop_dhcp(ls);

	report_link_down(ls);
}

void link_gone(LS)
{
	stop_link_procs(ls, 1);

	if(ls->flags & LF_UNSAVED)
		save_link(ls);

	report_link_gone(ls);
}

void link_exit(LS, int tag, int status)
{
	if(tag == CH_DHCP)
		dhcp_exit(ls, status);
	else if(tag == CH_WIENC)
		wsupp_exit(ls, status);

	maybe_mark_stopped(ls);
}

void link_flushed(LS)
{
	ls->flags &= ~LF_FLUSHING;

	maybe_mark_stopped(ls);

	if(ls->flags & LF_DHCPREQ)
		start_dhcp(ls);
}

/* Check for and renew DHCP leases when appropriate.

   A very simplistic solution that assumes all addresses on a link
   have the same lifetime. That should never happen with common IPv4
   DHCP setups, but Linux actually allow weird configurations like that.

   Anyway, doing it better will likely require much more involved
   address tracking, and current scheme will at most cause us to renew
   leases a earlier than necessary.

   Alternatively, switching to a long-running DHCP client may be an option. */

static struct timespec lastcheck;

static void reset_link_timer(void)
{
	struct link* ls;
	long sec = 0;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;
		if(!ls->lease)
			continue;
		if(sec && ls->lease > sec)
			continue;

		sec = ls->lease;
	}

	set_timeout(sec);
}

void link_lease(LS, uint time)
{
	if(ls->lease && ls->lease < time)
		return;

	ls->lease = time;

	reset_link_timer();
}

void timer_expired(void)
{
	struct timespec ts;
	struct link* ls;
	long diff;

	if((sys_clock_gettime(CLOCK_MONOTONIC, &ts)) < 0)
		return;

	if((diff = ts.sec - lastcheck.sec) <= 0)
		return;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;
		if(!ls->lease)
			continue;
		if(ls->lease > diff) {
			ls->lease -= diff;
			continue;
		}

		ls->lease = 0;
		start_dhcp(ls);
	}

	lastcheck = ts;

	reset_link_timer();
}
