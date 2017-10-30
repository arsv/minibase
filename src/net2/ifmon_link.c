#include <util.h>
#include <printf.h>
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

static void start_flush(LS)
{
	char* argv[] = { "ipcfg", "-f", ls->name, NULL };

	if(spawn(ls, CH_DHCP, argv) < 0) {
		ls->flags |= LF_FLUSHREQ;
	} else {
		ls->flags &= ~(LF_FLUSHREQ | LF_ADDRSET);
		ls->flags |= LF_FLUSHING;
	}
}

static void stop_dhcp(LS)
{
	kill_tagged(ls, CH_DHCP);
}

static void flush_link(LS)
{
	start_flush(ls);
}

static void dhcp_exit(LS, int status)
{
	if(ls->flags & LF_FLUSHING)
		ls->flags &= ~LF_FLUSHING;
	else if(status)
		ls->flags |= LF_DHCPFAIL;

	if(!(ls->flags & LF_FLUSHING))
		report_link_dhcp(ls, status);

	if(ls->flags & LF_FLUSHREQ)
		return start_flush(ls);
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
