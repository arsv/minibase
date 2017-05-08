#include <null.h>
#include <format.h>
#include <string.h>

#include "wimon.h"

struct wifi wifi;

static int allbits(int val, int bits)
{
	return ((val & bits) == bits);
}

static int anybits(int val, int bits)
{
	return ((val & bits));
}

static int wpa_ccmp_ccmp(struct scan* sc)
{
	return allbits(sc->type, ST_RSN_PSK | ST_RSN_P_CCMP | ST_RSN_G_CCMP);
}

static int wpa_ccmp_tkip(struct scan* sc)
{
	return allbits(sc->type, ST_RSN_PSK | ST_RSN_P_CCMP | ST_RSN_G_TKIP);
}

static int check_wpa(struct scan* sc)
{
	if(!allbits(sc->type, ST_RSN_PSK | ST_RSN_P_CCMP))
		return 0;
	if(!anybits(sc->type, ST_RSN_G_CCMP | ST_RSN_G_TKIP))
		return 0;

	return 1;
}

static void check_new_aps(int ifi)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(sc->ifi != ifi)
			continue;
		if(sc->flags & SF_SEEN)
			continue;

		sc->flags |= SF_SEEN;
		sc->prio = saved_psk_prio(sc->ssid, sc->slen);

		if(check_wpa(sc))
			sc->flags |= SF_GOOD;
	}
}

static int check_ssid(struct scan* sc)
{
	if(!(wifi.mode & (WM_APLOCK | WM_RETRY)))
		return 1; /* any ap will do */
	if(sc->slen != wifi.slen)
		return 0;
	if(memcmp(sc->ssid, wifi.ssid, sc->slen))
		return 0;
	return 1;
}

static struct scan* get_best_ap(void)
{
	struct scan* sc;
	struct scan* best = NULL;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(sc->ifi <= 0)
			continue;
		if(sc->flags & SF_TRIED && !(wifi.mode & WM_RETRY))
			continue;
		if(!check_ssid(sc))
			continue;
		else if(sc->prio <= 0)
			continue;
		if(!(sc->flags & SF_GOOD))
			continue;
		if(best && best->prio > sc->prio)
			continue;
		if(best && best->signal > sc->signal)
			continue;

		best = sc;
	};

	return best;
}

static int any_ongoing_scans(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(ls->scan)
			return 1;

	return 0;
}

static int load_ap(struct scan* sc)
{
	if(!load_psk(sc->ssid, sc->slen, wifi.psk, sizeof(wifi.psk)))
		return 0;

	wifi.ifi = sc->ifi;
	wifi.freq = sc->freq;
	wifi.slen = sc->slen;
	memcpy(wifi.ssid, sc->ssid, sc->slen);

	return 1;
}

static int connect_to_something(void)
{
	struct scan* sc;
	struct link* ls;

	while((sc = get_best_ap())) {
		sc->flags |= SF_TRIED;

		eprintf("best ap %s\n", sc->ssid);

		if(!(ls = find_link_slot(sc->ifi)))
			continue;
		if(!load_ap(sc))
			continue;

		if(wpa_ccmp_ccmp(sc))
			spawn_wpa(ls, sc, NULL, wifi.psk);
		else if(wpa_ccmp_tkip(sc))
			spawn_wpa(ls, sc, "ct", wifi.psk);
		else
			continue;

		return 1;
	};

	return 0;
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

static int rescan_for_current_ap(void)
{
	struct link* ls;

	if(!(ls = find_link_slot(wifi.ifi)))
		return 0;

	trigger_scan(ls, wifi.freq);

	return 1;
}

static void scan_all_wifis(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(scannable(ls))
			trigger_scan(ls, 0);
}

static void reassess_wifi_situation(void)
{
	eprintf("%s\n", __FUNCTION__);

	if(connect_to_something())
		return;

	if(wifi.mode & WM_RETRY) {
		wifi.mode &= ~WM_RETRY;
		scan_all_wifis();
		return;
	}

	if(wifi.mode & WM_NOSCAN)
		return;
	if(!any_active_wifis())
		return;

	eprintf("next scan in 60sec\n");
	schedule(scan_all_wifis, 60);
}

/* link_* are notification of link status changes from the NL code */

void link_new(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	load_link(ls);
}

void link_wifi(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(scannable(ls))
		trigger_scan(ls, 0);
}

void link_enabled(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(scannable(ls))
		trigger_scan(ls, 0);
}

void link_scan_done(struct link* ls)
{
	eprintf("%s %s\n", __FUNCTION__, ls->name);

	if(wifi.mode & WM_NOSCAN)
		return;
	if(any_ongoing_scans())
		return;

	check_new_aps(ls->ifi);
	reassess_wifi_situation();
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

	if(wifi.mode & WM_UNSAVED) {
		save_psk(wifi.ssid, wifi.slen, wifi.psk, strlen(wifi.psk));
		wifi.mode &= ~WM_UNSAVED;
	}

	wifi.mode &= ~WM_RETRY;
	wifi.mode |= WM_CONNECT;
}

void link_terminated(struct link* ls)
{
	eprintf("link_terminated %s\n", ls->name);

	if(ls->ifi != wifi.ifi) {
		eprintf("not our master wifi\n");
		return;
	}

	/* WM_RETRY here means we tried to reconnect but failed */
	if(wifi.mode & WM_RETRY)
		wifi.mode &= ~(WM_RETRY | WM_CONNECT);
	else if(wifi.mode & WM_CONNECT)
		wifi.mode |= WM_RETRY;

	if(wifi.mode & WM_RETRY)
		if(rescan_for_current_ap())
			return;

	reassess_wifi_situation();
}
