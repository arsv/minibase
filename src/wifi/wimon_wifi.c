#include <bits/errno.h>

#include <string.h>
#include <format.h>

#include "wimon.h"

struct wifi wifi;

static int allbits(int val, int bits)
{
	return ((val & bits) == bits);
}

static int start_wifi(void)
{
	int type = wifi.type;
	struct link* ls;

	if(!wifi.ifi)
		return -EINVAL;
	if(!(ls = find_link_slot(wifi.ifi)))
		return -EINVAL;

	if(allbits(type, ST_RSN_PSK | ST_RSN_P_CCMP | ST_RSN_G_CCMP))
		spawn_wpa(ls, NULL);
	else if(allbits(type, ST_RSN_PSK | ST_RSN_P_CCMP | ST_RSN_G_TKIP))
		spawn_wpa(ls, "ct");
	else
		return -EINVAL;

	return 0;
}

static int check_wpa(struct scan* sc)
{
	int type = sc->type;

	if(!(type & ST_RSN_PSK))
		return 0;
	if(!(type & ST_RSN_P_CCMP))
		return 0;
	if(!(type & (ST_RSN_G_CCMP | ST_RSN_G_TKIP)))
		return 0;

	return 1;
}

static void check_new_aps(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(sc->flags & SF_SEEN)
			continue;

		sc->flags |= SF_SEEN;
		sc->prio = saved_psk_prio(sc->ssid, sc->slen);

		if(!check_wpa(sc))
			continue;

		sc->flags |= SF_GOOD;
	}
}

static int match_ssid(uint8_t* ssid, int slen, struct scan* sc)
{
	if(!ssid || !slen)
		return 1;
	if(sc->slen != slen)
		return 0;
	if(memcmp(sc->ssid, ssid, slen))
		return 0;
	return 1;
}

static struct scan* get_best_ap(uint8_t* ssid, int slen)
{
	struct scan* sc;
	struct scan* best = NULL;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(sc->tries >= 3)
			continue;
		if(sc->prio <= 0)
			continue;
		if(!match_ssid(ssid, slen, sc))
			continue;
		if(!(sc->flags & SF_GOOD))
			continue;

		if(!best)
			goto set;

		if(best->prio > sc->prio)
			continue;
		if(best->signal > sc->signal)
			continue;
		if(best->tries < sc->tries)
			continue;

		set: best = sc;
	};

	if(best)
		best->tries++;

	return best;
}

static void reset_scan_counters(uint8_t* ssid, int slen)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(!match_ssid(ssid, slen, sc))
			continue;

		sc->tries = 0;
	}
}

static int load_ap_psk(void)
{
	return load_psk(wifi.ssid, wifi.slen, wifi.psk, sizeof(wifi.psk));
}

static int load_ap(struct scan* sc)
{
	wifi.freq = sc->freq;
	wifi.slen = sc->slen;
	wifi.type = sc->type;
	memcpy(wifi.bssid, sc->bssid, sizeof(sc->bssid));
	memcpy(wifi.ssid, sc->ssid, sc->slen);

	return load_ap_psk();
}

static void reset_ap_psk(void)
{
	memset(&wifi.psk, 0, sizeof(wifi.psk));
}

static void reset_wifi_struct(void)
{
	wifi.state = 0;
	wifi.flags = 0;
	wifi.ifi = 0;
	wifi.freq = 0;
	wifi.slen = 0;
	wifi.type = 0;

	memset(&wifi.ssid, 0, sizeof(wifi.ssid));
	memset(&wifi.bssid, 0, sizeof(wifi.bssid));

	reset_ap_psk();
}

static int connect_to_something(void)
{
	struct scan* sc;
	uint8_t* ssid;
	int slen;

	if(wifi.mode == WM_FIXEDAP) {
		ssid = wifi.ssid;
		slen = wifi.slen;
	} else {
		ssid = NULL;
		slen = 0;
	}

	while((sc = get_best_ap(ssid, slen))) {
		eprintf("best ap %s\n", sc->ssid);
		if(load_ap(sc))
			continue;
		if(start_wifi())
			continue;
		return 1;
	}

	reset_ap_psk();

	return 0;
}

int grab_wifi_device(int rifi)
{
	struct link* ls;
	int ifi = wifi.ifi;

	if(ifi && (ls = find_link_slot(ifi)))
		return ifi;

	wifi.ifi = ifi = 0;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi || ls->mode == LM_NOT)
			continue;
		if(!(ls->flags & S_NL80211))
			continue;

		if(!ifi)
			ifi = ls->ifi;
		else
			return -EMLINK;
	} if(!ifi)
		return -ENODEV;

	wifi.ifi = ifi;

	return ifi;
}

static void start_wifi_scan(void)
{
	if(wifi.ifi)
		trigger_scan(wifi.ifi, 0);
}

static void reassess_wifi_situation(void)
{
	eprintf("%s\n", __FUNCTION__);

	if(wifi.mode == WM_DISABLED)
		return;

	if(connect_to_something())
		return;

	unlatch(WIFI, CONF, -ESRCH);

	if(!wifi.ifi)
		return;

	eprintf("next scan in 60sec\n");
	schedule(start_wifi_scan, 60);
}

static struct scan* find_current_ap(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(memcmp(sc->bssid, wifi.bssid, sizeof(wifi.bssid)))
			continue;

		return sc;
	}

	return NULL;
}

/* This gets calls when there's a new 802.11 which _wifi.c may or may not
   claim as its primary interface.

   If we find the device in *connected* state during startup, and we want
   to claim it, blindly send disconnect request and hope it will complete
   before the scan does. It's bad but attempting to handle it properly
   results in even worse code. During normal operations, lingering aplocks
   are dealt with in terminate_link(). */

void wifi_ready(struct link* ls)
{
	eprintf("%s\n", __FUNCTION__);

	if(ls->ifi != wifi.ifi)
		return;
	if(wifi.mode == WM_DISABLED)
		return;
	if(wifi.state != WS_NONE)
		return;

	if(ls->flags & S_APLOCK)
		trigger_disconnect(ls->ifi);

	trigger_scan(ls->ifi, 0);
}

void wifi_gone(struct link* ls)
{
	if(ls->ifi != wifi.ifi)
		return;

	wifi.ifi = 0;
	wifi.state = WS_NONE;
}

static void retry_current_ap(void)
{
	wifi.state = WS_NONE;

	eprintf("%s\n", __FUNCTION__);

	if(load_ap_psk())
		goto out;
	if(start_wifi())
		goto out;
	return;
out:
	reset_wifi_struct();
	reassess_wifi_situation();
}

void wifi_scan_done(void)
{
	eprintf("%s\n", __FUNCTION__);

	check_new_aps();
	unlatch(WIFI, SCAN, 0);

	if(wifi.state == WS_RETRYING)
		retry_current_ap();
	else if(wifi.state == WS_NONE)
		reassess_wifi_situation();
}

void wifi_scan_fail(int err)
{
	unlatch(WIFI, SCAN, err);

	if(wifi.state == WS_RETRYING)
		wifi.state = WS_NONE;
}

void wifi_connected(struct link* ls)
{
	struct scan* sc;

	eprintf("%s\n", __FUNCTION__);

	if(ls->ifi != wifi.ifi)
		return;

	unlatch(WIFI, CONF, 0);

	if((sc = find_current_ap()))
		sc->tries = 0;

	if(wifi.flags & WF_UNSAVED) {
		save_psk(wifi.ssid, wifi.slen, wifi.psk, strlen(wifi.psk));
		wifi.flags &= ~WF_UNSAVED;
	}

	wifi.state = WS_CONNECTED;
}

void wifi_conn_fail(struct link* ls)
{
	eprintf("%s\n", __FUNCTION__);

	if(ls->ifi != wifi.ifi)
		return; /* stray wifi interface */

	if(wifi.mode == WM_DISABLED)
		wifi.state = WS_NONE;
	if(wifi.state == WS_RETRYING)
		wifi.state = WS_NONE;
	if(wifi.state == WS_CONNECTED) {
		wifi.state = WS_RETRYING;
		trigger_scan(wifi.ifi, wifi.freq);
		return;
	}

	reset_wifi_struct();
	reassess_wifi_situation();
}

void wifi_mode_disabled(void)
{
	wifi.mode = WM_DISABLED;
	reset_wifi_struct();
}

int wifi_mode_roaming(void)
{
	if(wifi.state)
		return -EBUSY;

	reset_scan_counters(NULL, 0);

	wifi.mode = WM_ROAMING;

	trigger_scan(wifi.ifi, 0);

	return 0;
}

int wifi_mode_fixedap(uint8_t* ssid, int slen)
{
	struct scan* sc;
	int ret;

	if(wifi.state)
		return -EBUSY;

	reset_scan_counters(ssid, slen);

	if(!(sc = get_best_ap(ssid, slen)))
		return -ENOENT;
	if((ret = load_ap(sc)))
		return ret;
	if((ret = start_wifi()))
		return ret;

	wifi.mode = WM_FIXEDAP;

	return 0;
}
