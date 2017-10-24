#include <util.h>
#include <printf.h>
#include "nimon.h"

static void start_wienc(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	char* argv[] = { "wienc", ls->name, NULL };

	if(spawn(ls, CH_OTHER, argv) < 0)
		return;

	ls->state = LS_RUNNING;
}

static void start_dhcp(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	char* argv[] = { "dhcp", ls->name, NULL };

	if(spawn(ls, CH_DHCP, argv) < 0)
		return;

	ls->dhcp = LD_RUNNING;
	ls->dhreq = 0;
}

static void start_flush(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	char* argv[] = { "ipcfg", "-f", ls->name, NULL };

	if(spawn(ls, CH_DHCP, argv) < 0)
		return;

	ls->dhcp = LD_FLUSHING;
}

void dhcp_link(LS)
{
	tracef("%s %s state %i\n", __FUNCTION__, ls->name, ls->dhcp);

	if(ls->dhcp != LD_NEUTRAL)
		ls->dhreq = 1;
	else
		start_dhcp(ls);
}

static void stop_dhcp(LS)
{
	if(ls->dhcp != LD_RUNNING)
		return;
	if(kill_tagged(ls, CH_DHCP) <= 0)
		return;

	ls->dhcp = LD_STOPPING;
}

static void flush_link(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	stop_dhcp(ls);

	switch(ls->dhcp) {
		case LD_NEUTRAL:  return; /* no need to flush */
		case LD_FLUSHING: return; /* already doing so */
		case LD_FINISHED: break;
		case LD_RUNNING:
			if(kill_tagged(ls, CH_DHCP) <= 0)
				break;
			/* fallthrough */
		case LD_STOPPING: ls->dhcp = LD_ST_FLUSH; /* fallthrough */
		case LD_ST_FLUSH: return; /* already requested */
		default:
			warn("unexpected dhcp state", NULL, ls->dhcp);
			return;
	}

	start_flush(ls);
}

static void dhcp_exit(LS, int status)
{
	(void) status;

	tracef("%s %s\n", __FUNCTION__, ls->name);

	switch(ls->dhcp) {
		case LD_ST_FLUSH:
			start_flush(ls);
			return;
		case LD_FLUSHING:
		case LD_STOPPING:
			ls->dhcp = LD_NEUTRAL;
			return;
		case LD_RUNNING:
			ls->dhcp = LD_FINISHED;
			return;
	}
}

int is_neutral(LS)
{
	if(ls->mode != LM_SKIP)
		return 0;
	if(any_procs_left(ls))
		return 0;
	if(ls->dhcp != LD_NEUTRAL)
		return 0;

	return 1;
}

int stop_link(LS)
{
	ls->mode = LM_SKIP;

	if(kill_tagged(ls, CH_OTHER) > 0)
		return 0;

	if(ls->dhcp != LD_NEUTRAL) {
		stop_dhcp(ls);
		flush_link(ls);
		return 0;
	}

	return -EALREADY;
}

void link_new(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	load_link(ls);

	int isup = (ls->wire != LW_DISABLED);
	int mode = ls->mode;

	tracef("link ifi=%i name=%s isup=%i mode=%i\n", ls->ifi, ls->name, isup, mode);

	if(mode == LM_SKIP)
		return;
	if(mode == LM_DOWN) {
		if(isup)
			disable_iface(ls);
	} else {
		if(isup)
			link_enabled(ls);
		else
			enable_iface(ls);
	}
}

void link_enabled(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	switch(ls->mode) {
		case LM_WIENC:
			start_wienc(ls);
			break;
		case LM_SETIP:
			load_link_conf(ls);
			break;
	}

	report_link_enabled(ls);

	if(ls->wire != LW_CARRIER)
		return;

	link_carrier(ls);
}

void link_carrier(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode == LM_AUTO)
		dhcp_link(ls);

	report_link_carrier(ls);
}

void link_lost(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	ls->dhreq = 0;

	flush_link(ls);
}

void link_down(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	ls->dhreq = 0;

	stop_dhcp(ls);
}

void link_gone(LS)
{
	tracef("%s %s\n", __FUNCTION__, ls->name);

	ls->dhreq = 0;
	ls->mode = LM_SKIP;

	stop_link_procs(ls, 1);
}

void link_exit(LS, int tag, int status)
{
	tracef("%s %s tag %i status %i\n", __FUNCTION__, ls->name, tag, status);

	if(tag == CH_DHCP) {
		dhcp_exit(ls, status);
		report_link_dhcp(ls, status);
	} else if(status) {
		stop_link(ls);
	} else if(ls->wire != LW_DISABLED) {
		link_enabled(ls);
	}
	
}
