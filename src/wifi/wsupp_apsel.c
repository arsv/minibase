#include <string.h>

#include "wsupp.h"

/* AP selection code.

   In accordance with IEEE 802.11, this tool assumes that all APs
   sharing the same SSID are parts of the same network. When the user
   commands connection to a certain AP, this only means a fixed SSID.
   The supplicant is free to choose any BSSID advertising that SSID,
   and fall back to another one if connection fails. Limited roaming
   is possible as well. If connection is lost, the tool will try to
   re-connect to the same AP but failing that, attempts may be made
   with different APs of the same network.

   This assumption is quite important, half of the code below would
   not be there if we were targeting a fixed BSSID.

   At this moment, fixed BSSIDs are not supported at all.
   The standard doesn't require it, and as far as I can tell, the few
   cases where it could be useful are much better served by ../temp/wifi
   instead. */

#define TIME_TO_FG_SCAN 1*60
#define TIME_TO_BG_SCAN 5*60

/* IEs = Information Elements.

   Ref. IEEE 802.11-2012 8.4.2 Information elements,
                         8.4.2.27 RSNE

   The following two tell the AP which ciphers we'd like to use.
   We send them twice: first in ASSOCIATE request, and then also
   in EAPOL packet 3/4. No idea why, but it must be done like that. */

struct ies {
	byte type;
	byte len;
	byte payload[];
};

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

/* Scans provide IEs for APs in range. These should contain SSID
   and the cipher sets supported by the APs. We have to do some
   parsing here to match those against our current ap.ssid and
   the ciphers we support.

   Parsing is done only when needed (that is, when we are choosing
   next AP to connect to) and results are kept specific to current
   struct ap settings. Most scan results, most of the time, do not
   get parsed because we're either idle or happily connected.

   In case some AP decides to change its IEs without going silent
   (or does so in-between our scans), we clear some of the flags
   set here in reset_ies_data() to force re-parsing on the next
   call to reassess_wifi_situation().

   Ref. IEEE 802.11-2012 8.4.2 Information elements, 8.4.2.27 RSNE */

static uint get2le(byte* p, byte* e)
{
	if(p + 3 > e)
		return 0;

	return (p[0] & 0xFF)
	    | ((p[1] & 0xFF) << 8);
}

static uint get4be(byte* p, byte* e)
{
	if(p + 4 > e)
		return 0;

	return (p[0] << 24)
	     | (p[1] << 16)
	     | (p[2] <<  8)
	     | (p[3]);
}

static uint find_int(byte* buf, int cnt, uint val)
{
	byte* p = buf;
	byte* e = buf + 4*cnt;

	for(; p < e; p += 4)
		if(get4be(p, e) == val)
			return 1;

	return 0;
}

static void parse_rsn_ie(struct scan* sc, int len, byte* buf)
{
	byte* p = buf;
	byte* e = buf + len;

	uint version = get2le(p, e);   p += 2;

	if(version != 1)
		return;

	uint group = get4be(p, e);     p += 4;
	uint pcnt = get2le(p, e);      p += 2;
	byte* pair = p;                p += 4*pcnt;
	uint acnt = get2le(p, e);      p += 2;
	byte* akms = p;              //p += 4*acnt;

	if(!(find_int(pair, pcnt, 0x000FAC04)))
		return; /* no pairwise CCMP */
	if(!(find_int(akms, acnt, 0x000FAC02)))
		return; /* no PSK AKM */

	if(group == 0x000FAC02) /* TKIP groupwise */
		sc->flags |=  SF_TKIP;
	else if(group == 0x000FAC04) /* CCMP groupwise */
		; /* we're good as is */
	else /* unsupported groupwise cipher */
		return;

	sc->flags |= SF_GOOD;
}

static struct ies* find_ie(void* buf, uint len, int type)
{
	void* ptr = buf;
	void* end = buf + len;

	while(ptr < end) {
		struct ies* ie = ptr;
		int ielen = sizeof(*ie) + ie->len;

		if(ptr + ielen > end)
			break;
		if(ie->type == type)
			return ie;

		ptr += ielen;
	}

	return NULL;
}

static void check_ap_ies(struct scan* sc)
{
	struct ies* ie;
	byte* buf = sc->ies;
	uint len = sc->ieslen;

	sc->flags |= SF_SEEN;

	if(!(ie = find_ie(buf, len, 0)))
		return; /* no SSID */
	if(ie->len != ap.slen)
		return; /* wrong SSID */
	if(memcmp(ie->payload, ap.ssid, ap.slen))
		return; /* wrong SSID */

	if(!(ie = find_ie(buf, len, 48)))
		return; /* no RSN entry */

	parse_rsn_ie(sc, ie->len, ie->payload);
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
			continue; /* empty slot */
		if(!sc->ies)
			continue; /* scan dump in progress? */

		if(sc->flags & SF_TRIED)
			continue; /* already tried that */
		if(!(sc->flags & SF_SEEN))
			check_ap_ies(sc);
		if(!(sc->flags & SF_GOOD))
			continue; /* bad crypto */
		if(compare(sc, best) <= 0)
			continue;

		best = sc;
	}

	return best;
}

static void clear_ap_bssid(void)
{
	ap.success = 0;
	ap.rescans = 0;
	ap.freq = 0;

	memzero(&ap.bssid, sizeof(ap.bssid));
}

static void clear_ap_ssid(void)
{
	struct scan* sc;

	ap.slen = 0;
	ap.freq = 0;

	memzero(&ap.ssid, sizeof(ap.ssid));
	memzero(PSK, sizeof(PSK));

	for(sc = scans; sc < scans + nscans; sc++)
		sc->flags &= ~(SF_GOOD | SF_TKIP | SF_SEEN | SF_TRIED);
}

void reset_station(void)
{
	clear_ap_bssid();
	clear_ap_ssid();
}

static void set_current_ap(struct scan* sc)
{
	ap.success = 0;
	ap.freq = sc->freq;

	memcpy(ap.bssid, sc->bssid, MACLEN);

	if(sc->flags & SF_TKIP) {
		ap.txies = ies_ccmp_tkip;
		ap.iesize = sizeof(ies_ccmp_tkip);
		ap.tkipgroup = 1;
	} else {
		ap.txies = ies_ccmp_ccmp;
		ap.iesize = sizeof(ies_ccmp_ccmp);
		ap.tkipgroup = 0;
	}
}

static struct scan* find_current_ap(void)
{
	struct scan* sc;

	if(!nonzero(ap.bssid, 6))
		return NULL;

	for(sc = scans; sc < scans + nscans; sc++)
		if(!memcmp(sc->bssid, ap.bssid, 6))
			return sc;

	return NULL;
}

/* Fixed AP mode means fixed SSID, *not* fixed BSSID, and we should be
   ready to roam between multiple APs sharing the same SSID. There's
   almost no difference between roading and fixed mode except for AP
   selection rules. */

int set_station(byte* ssid, int slen, byte psk[32])
{
	if(slen > (int)sizeof(ap.ssid))
		return -ENAMETOOLONG;

	memcpy(ap.ssid, ssid, slen);
	ap.slen = slen;

	clear_ap_bssid();

	memcpy(PSK, psk, 32);

	return 0;
}

static int connect_to_something(void)
{
	struct scan* sc;
	int ret;

	while((sc = get_best_ap())) {
		set_current_ap(sc);

		if((ret = start_connection()) < 0)
			return ret;

		sc->flags |= SF_TRIED;

		return !!sc;
	}

	return !!sc;
}

void handle_connect(void)
{
	struct scan* sc;

	ap.success = 1;

	set_timer(TIME_TO_BG_SCAN);

	if(opermode == OP_RESCAN)
		opermode = OP_ACTIVE;
	if(opermode == OP_ONESHOT)
		opermode = OP_ACTIVE;

	if((sc = find_current_ap()))
		sc->flags &= ~SF_TRIED;

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

	reassess_wifi_situation();
}

/* Netlink reports AP connection has been lost. */

void handle_disconnect(void)
{
	clr_timer();

	report_disconnect();

	if(opermode == OP_DETACH) {
		/* we were disconnecting to drop the device */
		reset_device();
	} else if(opermode == OP_MONITOR) {
		/* we were simply disconnecting (wifi dc) */
		clear_ap_bssid();
		clear_ap_ssid();
	} else { /* we were not planning to disconnect at all */

		if(opermode == OP_RESCAN)     /* re-connect failed */
			opermode = OP_ACTIVE; /* try another BSS   */

		if(ap.success)                /* we were connected */
			rescan_current_ap();  /* try to re-connect */
		else /* we were *not* successful in connecting to this BSS */
			try_some_other_ap();
	}
}

/* Netlink reported ENETDOWN and we timed out waiting for rfkill. */

void handle_netdown(void)
{
	opermode = OP_DETACH;

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
	if(!ap.slen) {
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
