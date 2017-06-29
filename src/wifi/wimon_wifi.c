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
	if(!(wifi.psk[0]))
		return -EINVAL;
	if(!(ls = find_link_slot(wifi.ifi)))
		return -EINVAL;

	mark_starting(ls, 2);

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

static int connectable(struct scan* sc)
{
	if(!(sc->flags & SF_GOOD))
		return 0; /* bad crypto */
	if(wifi.mode == WM_FIXEDAP)
		return 1;
	if(sc->prio <= 0)
		return 0; /* no PSK */
	return 1;
}

static int match_ssid(struct scan* sc)
{
	if(wifi.mode != WM_FIXEDAP)
		return 1;
	if(sc->slen != wifi.slen)
		return 0;
	if(memcmp(sc->ssid, wifi.ssid, wifi.slen))
		return 0;
	return 1;
}

/* Impose slight preference for the 5GHz band. Only matters if there are
   several connectable APs (which is rare in itself) in different bands.

   Signal limit is to avoid picking weak 5GHz APs over strong 2GHz ones.
   The number is arbitrary and shouldn't matter much as long as it's high
   enough to not interfere with nearby APs.

   Hard-coded; making it controllable takes way more code than it's worth. */

static int band_score(struct scan* sc)
{
	if(sc->signal < -7500) /* -75dBm */
		return 0;
	if(sc->freq / 1000 == 5) /* 5GHz */
		return 1;
	return 0;
}

static int better(struct scan* sc, struct scan* best)
{
       if(!best)
               return 1;
       if(sc->prio > best->prio)
               return 1;
       if(band_score(sc) > band_score(best))
               return 1;
       if(sc->signal > best->signal)
               return 1;
       if(sc->tries < best->tries)
               return 1;

       return 0;
}

static struct scan* get_best_ap()
{
	struct scan* sc;
	struct scan* best = NULL;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(sc->tries >= 3)
			continue;
		if(!connectable(sc))
			continue;
		if(!match_ssid(sc))
			continue;
		if(!better(sc, best))
			continue;
		best = sc;
	}

	if(best)
		best->tries++;

	return best;
}

static void reset_scan_counters()
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(!match_ssid(sc))
			continue;

		sc->tries = 0;
	}
}

static void clear_ap(void)
{
	wifi.freq = 0;
	wifi.type = 0;

	if(wifi.mode != WM_FIXEDAP) {
		wifi.slen = 0;
		memset(&wifi.ssid, 0, sizeof(wifi.ssid));
		memset(&wifi.psk, 0, sizeof(wifi.psk));
	}

	memset(&wifi.bssid, 0, sizeof(wifi.bssid));
}

static int load_given_psk(char* psk)
{
	int len = strlen(psk);

	if(len > sizeof(wifi.psk) - 1)
		return -EINVAL;

	memcpy(wifi.psk, psk, len + 1);
	wifi.flags |= WF_NEWPSK;

	return 0;
}

static int load_saved_psk(void)
{
	return load_psk(wifi.ssid, wifi.slen, wifi.psk, sizeof(wifi.psk));
}

static int load_ap(struct scan* sc)
{
	int ret;

	wifi.freq = sc->freq;
	wifi.type = sc->type;
	memcpy(wifi.bssid, sc->bssid, sizeof(sc->bssid));

	if(wifi.mode != WM_FIXEDAP) {
		wifi.slen = sc->slen;
		memcpy(wifi.ssid, sc->ssid, sc->slen);
	};

	if(wifi.mode == WM_FIXEDAP && wifi.psk[0])
		return 0; /* no need to reload PSK for the same AP */

	if((ret = load_saved_psk()))
		clear_ap();

	return ret;
}

static int connect_to_something(void)
{
	struct scan* sc;

	while((sc = get_best_ap())) {
		if(load_ap(sc))
			continue;
		if(start_wifi())
			continue;
		return 1;
	}

	return 0;
}

int grab_wifi_device(int rifi)
{
	struct link* ls;
	int ifi = wifi.ifi;

	if(ifi && (ls = find_link_slot(ifi)))
		return ifi;
	if(ifi)
		unlatch(WIFI, ANY, -EINTR);

	wifi.ifi = ifi = 0;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;
		if(!(ls->flags & S_NL80211))
			continue;
		if(ls->mode == LM_NOT)
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

void start_wifi_scan(void)
{
	if(!wifi.ifi)
		return;

	if(wifi.state == WS_NONE || wifi.state == WS_IDLE)
		wifi.state = WS_SCANNING;

	trigger_scan(wifi.ifi, 0);
}

static void timed_wifi_scan(int _)
{
	start_wifi_scan();
}

static void idle_then_rescan(void)
{
	clear_ap();
	wifi.state = WS_IDLE;
	schedule(60, timed_wifi_scan, WIFI);
}

static void snap_to_disabled(void)
{
	clear_ap();
	wifi.mode = WM_DISABLED;
}

static void reassess_wifi_situation(void)
{
	if(wifi.mode == WM_DISABLED)
		return;
	if(wifi.state == WS_CONNECTED)
		return;

	wifi.state = WS_STARTING;

	if(connect_to_something())
		return;

	unlatch(WIFI, CONF, -ENOTCONN);

	if(wifi.flags & WF_UNSAVED)
		snap_to_disabled();
	else
		idle_then_rescan();
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
	if(!wifi.ifi)
		load_wifi(ls);
	if(ls->ifi != wifi.ifi)
		return;
	if(wifi.mode == WM_DISABLED)
		return;
	if(wifi.state != WS_NONE)
		return;

	start_wifi_scan();
}

void wifi_gone(struct link* ls)
{
	if(ls->ifi != wifi.ifi)
		return;

	wifi.ifi = 0;
	wifi.state = WS_NONE;

	unlatch(WIFI, ANY, -ENODEV);
}

static void retry_current_ap(void)
{
	wifi.state = WS_RETRYING;

	if(start_wifi())
		goto out;
	return;
out:
	clear_ap();
	reassess_wifi_situation();
}

void wifi_scan_done(void)
{
	check_new_aps();
	unlatch(WIFI, SCAN, 0);

	if(wifi.state == WS_SCANNING)
		wifi.state = WS_NONE;
	if(wifi.state == WS_CONNECTED)
		return;
	if(wifi.state == WS_RETRYING)
		retry_current_ap();
	else
		reassess_wifi_situation();
}

void wifi_scan_fail(int err)
{
	unlatch(WIFI, SCAN, err);

	if(wifi.state == WS_SCANNING)
		wifi.state = WS_NONE;
	if(wifi.state == WS_RETRYING)
		wifi.state = WS_NONE;
}

static void maybe_save_wifi_state(void)
{
	if(wifi.flags & WF_NEWPSK)
		save_psk(wifi.ssid, wifi.slen, wifi.psk);
	if(wifi.flags & WF_UNSAVED)
		save_wifi();
	if(wifi.flags & (WF_UNSAVED | WF_NEWPSK))
		save_config();

	wifi.flags &= ~(WF_UNSAVED | WF_NEWPSK);
}

void wifi_connected(struct link* ls)
{
	struct scan* sc;

	if(ls->ifi != wifi.ifi)
		return;

	if((sc = find_current_ap()))
		sc->tries = 0;

	wifi.state = WS_CONNECTED;

	maybe_save_wifi_state();
	unlatch(WIFI, CONF, 0);
}

void wifi_conn_fail(struct link* ls)
{
	if(ls->ifi != wifi.ifi)
		return; /* stray wifi interface */

	if(wifi.mode == WM_DISABLED) {
		wifi.state = WS_NONE;
		return;
	}

	if(wifi.state == WS_RETRYING)
		wifi.state = WS_NONE;

	if(wifi.state == WS_CHANGING) {
		reassess_wifi_situation();
	} else if(wifi.state == WS_CONNECTED) {
		wifi.state = WS_RETRYING;
		trigger_scan(wifi.ifi, wifi.freq);
	} else {
		clear_ap();
		reassess_wifi_situation();
	}
}

static int restart_wifi(void)
{
	struct link* ls;
	int ws = wifi.state;

	if(!(ls = find_link_slot(wifi.ifi)))
		return -ENODEV;
	if(ls->state == LS_STOPPING)
		return 0;

	set_link_mode(ls, LM_DHCP);

	if(!(ls->flags & S_ENABLED))
		enable_iface(ls->ifi);
	else if(ls->flags & S_CARRIER)
		terminate_link(ls);

	if(ws == WS_SCANNING)
		return 0;
	if(ws == WS_NONE || ws == WS_IDLE) {
		start_wifi_scan();
		return 0;
	}

	wifi.state = WS_CHANGING;
	cancel_scheduled(WIFI);
	reset_scan_counters();

	return 0;
}

void wifi_mode_disabled(void)
{
	cancel_scheduled(WIFI);
	snap_to_disabled();
	save_wifi();

	/* the link gets stopped in _ctrl, no need to stop it here */
}

int wifi_mode_roaming(void)
{
	wifi.mode = WM_ROAMING;
	wifi.flags |= WF_UNSAVED;

	return restart_wifi();
}

int wifi_mode_fixedap(uint8_t* ssid, int slen, char* psk)
{
	int ret;

	if(slen > sizeof(wifi.ssid))
		return -EINVAL;
	if(!psk && saved_psk_prio(ssid, slen) < 0)
		return -ENOKEY;

	memcpy(wifi.ssid, ssid, slen);
	wifi.slen = slen;

	if(psk)
		ret = load_given_psk(psk);
	else
		ret = load_saved_psk();

	if(ret < 0) {
		snap_to_disabled();
	} else {
		wifi.mode = WM_FIXEDAP;
		wifi.flags |= WF_UNSAVED;
	}

	return restart_wifi();
}
