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

static void flush_link(LS)
{
	if(ls->flags & LF_FLUSHING)
		return;

	ls->flags &= ~LF_FLUSHREQ;
	ls->flags |= LF_FLUSHING;

	delete_addr(ls);
	stop_dhcp(ls);
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

static int maybe_mark_stopped(LS)
{
	if(!(ls->flags & LF_STOP))
		return 0;
	if(!(ls->flags & LF_RUNNING))
		return 0;
	if(ls->flags & LF_FLUSHING)
		return 0;
	if(any_procs_left(ls))
		return 0;

	ls->flags &= ~LF_RUNNING;

	return 1;
}

int stop_link(LS)
{
	if(!(ls->flags & LF_RUNNING))
		return -EALREADY;

	if(!(ls->flags & LF_STOP)) {
		ls->flags |= LF_STOP;
		stop_link_procs(ls, 0);
		flush_link(ls);
	}

	if(maybe_mark_stopped(ls))
		return -EALREADY;

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
	if(tag == CH_WIENC)
		wsupp_exit(ls, status);
	if(maybe_mark_stopped(ls))
		report_link_stopped(ls);
}

void link_flushed(LS)
{
	ls->flags &= ~LF_FLUSHING;

	if(maybe_mark_stopped(ls))
		report_link_stopped(ls);

	if(ls->flags & LF_DHCPREQ)
		start_dhcp(ls);
}
