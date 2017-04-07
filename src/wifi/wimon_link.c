#include <null.h>
#include <format.h>

#include "wimon.h"
#include "wimon_slot.h"
#include "wimon_proc.h"

#define WM_NOSCAN    (1<<0)
#define WM_CONNECT   (1<<1)
#define WM_APLOCK    (1<<2)
#define WM_RETRY     (1<<3)

struct wifi {
	int mode;
	int freq;
	int ifi;
	uint8_t ssid[SSIDLEN];
	char psk[2*32+1];
} wifi;

/* */

static int looks_connectable(struct scan* sc)
{
	if(!(sc->type & ST_RSN_PSK))
		return 0;
	if(!(sc->type & ST_RSN_P_CCMP))
		return 0;
	if(!(sc->type & (ST_RSN_G_CCMP | ST_RSN_G_TKIP)))
		return 0;

	return 1;
}

static struct scan* get_best_station(void)
{
	struct scan* sc;
	struct scan* best = NULL;
	int signal = 0;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(sc->seen)
			continue;
		if(sc->ifi <= 0)
			continue;
		if(!looks_connectable(sc)) /* XXX: move to link_scan_done */
			continue;
		if(signal && signal > sc->signal)
			continue;

		signal = sc->signal;
		best = sc;
	};

	return best;
}

static int any_ongoing_scans(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if((ls->mode & LM_SCANRQ) && ls->scan)
			return 1;

	return 0;
}

static int load_psk_for(struct scan* sc)
{
	return 0;
}

static void try_connect(struct link* ls, struct scan* sc)
{
	eprintf("try_connect %s %s\n", ls->name, sc->ssid);
}

static int maybe_connect_to_something(void)
{
	struct scan* sc;
	struct link* ls;

	while((sc = get_best_station())) {
		sc->seen = 1;

		if(!load_psk_for(sc))
			continue;
		if(!(ls = find_link_slot(sc->ifi)))
			continue;

		try_connect(ls, sc);

		return 1;
	};

	return 0;
}

static void scan_link(struct link* ls)
{
	ls->mode |= LM_SCANRQ;
	trigger_scan(ls);
}

static int scannable(struct link* ls)
{
	if(!ls->ifi)
		return 0;
	if(!(ls->flags & S_WIRELESS))
		return 0;
	if(ls->mode & (LM_NOTOUCH | LM_NOWIFI))
		return 0;
	return 1;
}

static int any_active_wifis(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(scannable(ls))
			return 1;

	return 0;
}

static void scan_all_wifis(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(scannable(ls))
			trigger_scan(ls);
}

static void reassess_wifi_situation(void)
{
	eprintf("%s\n", __FUNCTION__);

	if(maybe_connect_to_something())
		return;
	if(wifi.mode & WM_NOSCAN)
		return;
	if(!any_active_wifis())
		return;

	schedule(scan_all_wifis, 60);
}

/* link_* are callbacks for link status changes, called by the NL code */

void link_new(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	/* load settings */
	/* sets DISABLED, MANUAL flags, maybe also ips */
}

void link_wifi(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(scannable(ls))
		scan_link(ls);
}

void link_scan_done(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(wifi.mode & WM_NOSCAN)
		return;
	if(any_ongoing_scans())
		return;

	reassess_wifi_situation();
}

void link_deconfed(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode & LM_TERMRQ)
		terminate_link(ls);
	/* else we don't care */
}

void link_disconnected(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode & (LM_NOTOUCH))
		return;

	terminate_link(ls);
}

void link_del(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);
	
	drop_link_procs(ls);

	if(ls->mode & (LM_NOTOUCH))
		return;

	link_terminated(ls);
}

void link_terminated(struct link* ls)
{
	eprintf("link_terminated %s\n", ls->name);

	if(ls->ifi != wifi.ifi)
		return;

	if(wifi.mode & (WM_CONNECT | WM_RETRY)) {
		wifi.mode &= ~WM_RETRY;
	}

	reassess_wifi_situation();
}

void link_enabled(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(scannable(ls)) {
		scan_link(ls);
	} else if(ls->mode & LM_CCHECK) {
		ls->mode &= ~LM_CCHECK;
		/* XXX */
		eprintf("carrier check on %s: %s\n", ls->name, 
			(ls->flags & S_CARRIER) ? "yes" : "no");
	}
}

void link_carrier(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(ls->mode & LM_MANUAL)
		; /* XXX */
	else if(ls->mode & LM_LOCAL)
		spawn_dhcp(ls, "-g");
	else
		spawn_dhcp(ls, NULL);
}

void link_configured(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(wifi.ifi != ls->ifi)
		return;

	wifi.mode &= ~WM_RETRY;
	wifi.mode |= WM_CONNECT;
}
