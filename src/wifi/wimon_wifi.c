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

static int check_ssid(struct scan* sc)
{
	if(wifi.mode == WM_ROAMING)
		return 1; /* any ap will do */
	if(wifi.mode != WM_FIXEDAP)
		return 0; /* something's wrong */
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
		if(!sc->freq)
			continue;
		if(sc->tries >= 3)
			continue;
		if(sc->prio <= 0)
			continue;
		if(!check_ssid(sc))
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

	return best;
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

	while((sc = get_best_ap())) {
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

int start_wifi_scan(void)
{
	if(!wifi.ifi)
		return -ENODEV;

	trigger_scan(wifi.ifi, 0);

	return 0;
}

static void scan_all_wifis(void)
{
	start_wifi_scan();
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
	schedule(scan_all_wifis, 60);
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

void wifi_ready(struct link* ls)
{
	eprintf("%s\n", __FUNCTION__);

	if(ls->ifi != wifi.ifi)
		return;
	if(wifi.mode == WM_DISABLED)
		return;
	if(wifi.state != WS_NONE)
		return;

	trigger_scan(wifi.ifi, 0);
}

void wifi_gone(struct link* ls)
{
	if(ls->ifi != wifi.ifi)
		return;

	wifi.ifi = 0;
	wifi.state = WS_NONE;
}

void wifi_scan_done(void)
{
	eprintf("%s\n", __FUNCTION__);

	check_new_aps();
	unlatch(WIFI, SCAN, 0);

	if(wifi.state != WS_NONE)
		return;

	reassess_wifi_situation();
}

void wifi_scan_fail(int err)
{
	unlatch(WIFI, SCAN, err);
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
	struct scan* sc;

	eprintf("%s\n", __FUNCTION__);

	if(ls->ifi != wifi.ifi)
		return; /* stray wifi interface */
	if((sc = find_current_ap()))
		sc->tries++;

	if(wifi.mode == WM_DISABLED)
		wifi.state = WS_NONE;

	if(wifi.state == WS_CONNECTED) {
		wifi.state = WS_RETRYING;

		if(load_ap_psk())
			goto reset;
		if(start_wifi())
			goto reset;

		return;
	}
reset:
	reset_wifi_struct();
	reassess_wifi_situation();
}
