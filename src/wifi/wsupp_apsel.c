#include <string.h>
#include <printf.h>

#include "wsupp.h"

/* AP selection code. Scan, pick some AP to connect, fail, pick
   another AP and so on. */

#define TIME_TO_FG_SCAN 1*60
#define TIME_TO_BG_SCAN 5*60

/* IEs (Information Elements) telling the AP which cipher we'd like to use
   must be sent twice: first in ASSOCIATE request, and then also in EAPOL
   packet 3/4. No idea why, but it must be done like that. Cipher selection
   depends on scan data anyway, so we do it here, and let netlink/eapol code
   pick the IEs from struct ap whenever appropriate.

   Ref. IEEE 802.11-2012 8.4.2.27 RSNE */

const char ies_ccmp_ccmp[] = {
	0x30, 0x14, /* ies { type = 48, len = 20 } */
	    0x01, 0x00, /* version 1 */
	    0x00, 0x0F, 0xAC, 0x04, /* CCMP group data chipher */
	    0x01, 0x00, /* pairwise chipher suite count */
	    0x00, 0x0F, 0xAC, 0x04, /* CCMP pairwise chipher */
	    0x01, 0x00, /* authentication and key management */
	    0x00, 0x0F, 0xAC, 0x02, /* PSK and RSNA key mgmt */
	    0x00, 0x00, /* preauth capabilities */
};

const char ies_ccmp_tkip[] = {
	0x30, 0x14,      /* everything's the same, except for: */
	    0x01, 0x00,
	    0x00, 0x0F, 0xAC, 0x02, /* TKIP group data chipher */
	    0x01, 0x00,
	    0x00, 0x0F, 0xAC, 0x04,
	    0x01, 0x00,
	    0x00, 0x0F, 0xAC, 0x02,
	    0x00, 0x00,
};

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

static int connectable(struct scan* sc)
{
	if(!(sc->flags & SF_GOOD))
		return 0; /* bad crypto */
	if(sc->flags & SF_TRIED)
		return 0; /* already tried that */
	if(ap.fixed)
		return 1;
	if(!(sc->flags & SF_PASS))
		return 0;
	return 1;
}

static int match_ssid(struct scan* sc)
{
	if(!ap.fixed)
		return 1;
	if(sc->slen != ap.slen)
		return 0;
	if(memcmp(sc->ssid, ap.ssid, ap.slen))
		return 0;
	return 1;
}

/* Impose slight preference for the 5GHz band. Only matters if there are
   several connectable APs (which is rare in itself) in different bands.

   Signal limit is set to avoid picking weak 5GHz APs over strong 2GHz ones.
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

static int cmp(int a, int b)
{
	if(a > b)
		return 1;
	if(a < b)
		return -1;
	return 0;
}

static int compare(struct scan* sc, struct scan* best)
{
	int r;

	if(!best)
		return 1;
	if((r = cmp(band_score(sc), band_score(best))))
		return r;
	if((r = cmp(sc->signal, best->signal)))
		return r;

	return 0;
}

/* We never sort the scanlist, we just pick the best non-marked AP,
   mark it and try to connect until there's no more APs left. */

static struct scan* get_best_ap(void)
{
	struct scan* sc;
	struct scan* best = NULL;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(!connectable(sc))
			continue;
		if(!match_ssid(sc))
			continue;
		if(compare(sc, best) <= 0)
			continue;
		best = sc;
	}

	return best;
}

static void clear_ap_bssid(void)
{
	ap.type = 0;
	ap.success = 0;
	ap.rescans = 0;

	memzero(&ap.bssid, sizeof(ap.bssid));
}

static void clear_ap_ssid(void)
{
	ap.slen = 0;
	ap.freq = 0;
	ap.fixed = 0;
	memzero(&ap.ssid, sizeof(ap.ssid));
	memzero(PSK, sizeof(PSK));
	ap.unsaved = 0;
}

void reset_station(void)
{
	clear_ap_bssid();
	clear_ap_ssid();
}

static int set_current_ap(struct scan* sc)
{
	int auth = sc->type;

	sc->flags |= SF_TRIED;

	ap.success = 0;
	ap.freq = sc->freq;
	ap.type = sc->type;
	memcpy(ap.bssid, sc->bssid, MACLEN);

	if(!(auth & ST_RSN_P_CCMP))
		return -1;

	if(auth & ST_RSN_G_TKIP) {
		ap.ies = ies_ccmp_tkip;
		ap.iesize = sizeof(ies_ccmp_tkip);
		ap.tkipgroup = 1;
	} else {
		ap.ies = ies_ccmp_ccmp;
		ap.iesize = sizeof(ies_ccmp_ccmp);
		ap.tkipgroup = 0;
	}

	if(ap.fixed)
		return 0;

	ap.slen = sc->slen;
	memcpy(ap.ssid, sc->ssid, sc->slen);

	if(load_psk(ap.ssid, ap.slen, PSK))
		return -1;

	return 0;
}

static struct scan* find_current_ap(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(sc->slen != ap.slen)
			continue;
		if(memcmp(sc->ssid, ap.ssid, ap.slen))
			continue;
		if(memcmp(sc->bssid, ap.bssid, 6))
			continue;

		return sc;
	}

	return NULL;
}

/* Fixed AP mode means fixed SSID, *not* fixed BSSID, and we should be
   ready to roam between multiple APs sharing the same SSID. There's
   almost no difference between roading and fixed mode except for AP
   selection rules. */

static void reset_scan_counters()
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(!match_ssid(sc))
			continue;

		sc->flags &= ~SF_TRIED;
	}
}

static int set_fixed(byte* ssid, int slen)
{
	if(slen > (int)sizeof(ap.ssid))
		return -ENAMETOOLONG;

	memcpy(ap.ssid, ssid, slen);
	ap.slen = slen;

	ap.fixed = 1;

	clear_ap_bssid();
	reset_scan_counters();

	return 0;
}

int set_fixed_given(byte* ssid, int slen, byte psk[32])
{
	int ret;

	if((ret = set_fixed(ssid, slen)) < 0)
		return ret;

	memcpy(PSK, psk, 32);

	ap.unsaved = 1;

	return 0;
}

int set_fixed_saved(byte* ssid, int slen)
{
	int ret;

	if((ret = load_psk(ssid, slen, PSK)) < 0)
		return ret;
	if((ret = set_fixed(ssid, slen)) < 0)
		return ret;

	ap.unsaved = 0;

	return 0;
}

static int connect_to_something(void)
{
	struct scan* sc;

	while((sc = get_best_ap())) {
		if(set_current_ap(sc))
			;
		else if(start_connection())
			;
		else return 1;

		sc->flags &= ~SF_GOOD;
	}

	return 0;
}

void handle_connect(void)
{
	struct scan* sc;

	ap.success = 1;
	ap.fixed = 1;

	set_timer(TIME_TO_BG_SCAN);

	if(opermode == OP_RESCAN)
		opermode = OP_ACTIVE;
	if(opermode == OP_ONESHOT)
		opermode = OP_ACTIVE;

	if((sc = find_current_ap()))
		sc->flags &= ~SF_TRIED;
	if(ap.unsaved)
		save_psk(ap.ssid, ap.slen, PSK);
	if(ap.unsaved && sc)
		sc->flags |= SF_PASS;

	ap.unsaved = 0;

	trigger_dhcp();

	report_connected();
}

static void rescan_current_ap(void)
{
	ap.success = 0;
	/* keep the rest of ap in place */
	opermode = OP_RESCAN;

	start_scan(ap.freq);
}

void reconnect_to_current_ap(void)
{
	if(opermode == OP_RESCAN)
		opermode = OP_ACTIVE;

	if(find_current_ap())
		start_connection();
	else
		reassess_wifi_situation();
}

static void try_some_other_ap(void)
{
	clear_ap_bssid();

	if(!ap.fixed)
		clear_ap_ssid();

	reassess_wifi_situation();
}

/* Netlink has completed a scan dump and wants us to evaluate the results. */

void check_new_scan_results(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		if(sc->flags & SF_SEEN)
			continue;

		sc->flags |= SF_SEEN;

		if(check_wpa(sc))
			sc->flags |= SF_GOOD;
		else
			continue;
		if(got_psk_for(sc->ssid, sc->slen))
			sc->flags |= SF_PASS;
	}
}

/* Netlink reports AP connection has been lost. */

void handle_disconnect(void)
{
	clr_timer();

	report_disconnect();

	if(opermode == OP_RESCAN)
		opermode = OP_ACTIVE;
	if(opermode == OP_ACTIVE) {
		if(ap.success)
			rescan_current_ap();
		else
			try_some_other_ap();
	} else {
		clear_ap_bssid();
		clear_ap_ssid();
		report_no_connect();
	}

	if(opermode == OP_IDLE)
		reset_device();
}

/* Netlink reported ENETDOWN and we timed out waiting for rfkill. */

void handle_netdown(void)
{
	opermode = OP_IDLE;

	handle_disconnect();
}

/* RFkill code reports the interface to be back online.

   Note that if rfkill happened on a live connection, disconnect will
   happen as usual and it will be the re-scan attempt that will fail
   with -ENETDOWN. In this case we will see OP_RESCAN. */

void handle_rfrestored(void)
{
	if(authstate != AS_NETDOWN)
		return; /* weren't connected before rfkill */

	authstate = AS_IDLE;

	if(opermode == OP_RESCAN)
		rescan_current_ap();
	else
		reassess_wifi_situation();
}

/* Foreground scan means scanning while not connected,
   background respectively means there's an active connection. */

void routine_bg_scan(void)
{
	start_void_scan();
	set_timer(TIME_TO_BG_SCAN);
}

void routine_fg_scan(void)
{
	if(!ap.fixed) {
		set_timer(TIME_TO_FG_SCAN);
		start_void_scan();
	} else if(ap.freq) {
		set_timer(10);

		if(++ap.rescans % 6)
			start_scan(ap.freq);
		else
			start_full_scan();

		if(ap.rescans >= 6*5) { /* 5 minutes */
			ap.freq = 0;
			ap.rescans = 0;
		}
	} else {
		set_timer(TIME_TO_FG_SCAN);
		start_full_scan();
	}
}

static void snap_to_neutral(void)
{
	clear_ap_bssid();
	clear_ap_ssid();
	opermode = OP_MONITOR;
}

static void idle_then_rescan(void)
{
	set_timer(TIME_TO_FG_SCAN);
}

void reassess_wifi_situation(void)
{
	if(opermode == OP_MONITOR)
		return;
	if(authstate != AS_IDLE)
		return;
	if(scanstate != SS_IDLE)
		return;

	if(connect_to_something())
		return;

	report_no_connect();

	if(opermode == OP_ONESHOT)
		snap_to_neutral();
	else
		idle_then_rescan();
}
