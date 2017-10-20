#include <sys/time.h>

#include <string.h>
#include <printf.h>

#include "wienc.h"

/* AP selection code. Scan, pick some AP to connect, fail, pick
   another AP, success, and so on. */

static struct timespec lastscan;

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
	ap.freq = 0;
	ap.type = 0;
	ap.success = 0;

	memzero(&ap.bssid, sizeof(ap.bssid));
}

static void clear_ap_ssid(void)
{
	ap.slen = 0;
	memzero(&ap.ssid, sizeof(ap.ssid));
	memzero(PSK, sizeof(PSK));
}

static int set_current_ap(struct scan* sc)
{
	int auth = sc->type;

	tracef("%s\n", __FUNCTION__);

	sc->flags |= SF_TRIED;

	ap.success = 0;
	ap.freq = sc->freq;
	ap.type = sc->type;
	memcpy(ap.bssid, sc->bssid, MACLEN);

	if(!(auth & ST_RSN_P_CCMP)) {
		tracef("%s no CCMP pair\n", __FUNCTION__);
		return -1;
	}

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

	if(load_psk(ap.ssid, ap.slen, PSK)) {
		tracef("%s failed to load PSK\n", __FUNCTION__);
		return -1;
	}

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
   ready roam between multiple APs sharing the same SSID. There's almost
   no difference between roading and fixed mode except for AP selection
   rules. */

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
	if(slen > sizeof(ap.ssid))
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

	return 0;
}

int set_fixed_saved(byte* ssid, int slen)
{
	int ret;

	if((ret = load_psk(ssid, slen, PSK)) < 0)
		return ret;
	if((ret = set_fixed(ssid, slen)) < 0)
		return ret;

	return 0;
}

static int connect_to_something(void)
{
	struct scan* sc;

	while((sc = get_best_ap())) {
		tracef("best AP %.*s\n", sc->slen, sc->ssid);

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

	tracef("%s\n", __FUNCTION__);

	ap.success = 1;

	if(opermode == OP_RESCAN)
		opermode = OP_ENABLED;

	if((sc = find_current_ap()))
		sc->flags &= ~SF_TRIED;

	report_connected();
}

static void rescan_current_ap(void)
{
	tracef("%s\n", __FUNCTION__);

	ap.success = 0;
	/* keep the rest of ap in place */
	opermode = OP_RESCAN;

	start_scan(ap.freq);
}

void reconnect_to_current_ap(void)
{
	tracef("%s\n", __FUNCTION__);

	if(opermode == OP_RESCAN)
		opermode = OP_ENABLED;

	if(find_current_ap())
		start_connection();
	else
		reassess_wifi_situation();
}

static void try_some_other_ap(void)
{
	tracef("%s\n", __FUNCTION__);

	clear_ap_bssid();

	if(!ap.fixed)
		clear_ap_ssid();

	reassess_wifi_situation();
}

/* Netlink has completed a scan dump and calls this to evaluate
   the results. */

void check_new_scan_results(void)
{
	struct scan* sc;

	tracef("%s\n", __FUNCTION__);

	for(sc = scans; sc < scans + nscans; sc++) {
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
	if(opermode == OP_EXITREQ)
		opermode = OP_EXIT;
	if(opermode == OP_EXIT)
		return;

	report_disconnect();

	if(opermode == OP_RESCAN)
		opermode = OP_ENABLED;

	if(opermode == OP_ONESHOT) {
		if(ap.success) {
			opermode = OP_ENABLED;
			ap.fixed = 1;
		}
	}

	if(opermode == OP_ENABLED) {
		if(ap.success)
			rescan_current_ap();
		else
			try_some_other_ap();
	}
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

	tracef("%s opermode %i\n", __FUNCTION__, opermode);

	if(opermode == OP_RESCAN)
		rescan_current_ap();
	else
		reassess_wifi_situation();
}

int run_stamped_scan(void)
{
	int ret;

	if((ret = start_scan(0)) < 0)
		return ret;

	sys_clock_gettime(CLOCK_MONOTONIC, &lastscan);

	return 0;
}

static int maybe_start_scan(void)
{
	struct timespec currtime;
	int ret;

	if((ret = sys_clock_gettime(CLOCK_MONOTONIC, &currtime)) < 0)
		return 0;
	if(currtime.sec == 0 && currtime.nsec == 0)
		return 0;

	if(lastscan.sec == 0 && lastscan.nsec == 0)
		;
	else if(currtime.sec < lastscan.sec)
		; /* something's wrong */
	else if(currtime.sec - lastscan.sec >= 50)
		; /* more than a minute has passed */
	else return 0;

	report_scanning();

	if((ret = start_scan(0)) < 0)
		return 0;

	lastscan = currtime;

	return 1;
}

static void snap_to_neutral(void)
{
	tracef("%s\n", __FUNCTION__);
	clear_ap_bssid();
	clear_ap_ssid();
	opermode = OP_NEUTRAL;
}

static void idle_then_rescan(void)
{
	tracef("%s\n", __FUNCTION__);
	set_timer(60); /* for rescan */
}

void reassess_wifi_situation(void)
{
	tracef("%s\n", __FUNCTION__);

	if(opermode == OP_NEUTRAL)
		return;
	if(authstate != AS_IDLE)
		return;
	if(scanstate != SS_IDLE)
		return;

	if(maybe_start_scan())
		return;
	if(connect_to_something())
		return;

	report_no_connect();

	if(opermode == OP_ONESHOT)
		snap_to_neutral();
	else
		idle_then_rescan();
}
