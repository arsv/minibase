#include <string.h>

#include "wsupp.h"

/* AP BSS code. Once the network (SSID) has been set up, we need to go
   through the scanlist and pick a station (BSSID) we'd like to use.
   This part is quite involved because we parse scanned IEs to check
   which stations support the encryption modes we want.

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

   At this moment, there is no support for fixed BSSID at all.
   The standard doesn't require it, and as far as I can tell, the few
   cases where it could be useful are much better served by ../temp/wifi
   instead. */

/* Note doing this in the client (for instance, supplying SSID and a set
   of BSSIDs the service should connect to) would not work if BSS'es are
   allowed to appear dynamically. Think of walking into a large building,
   connecting to the first AP visible, and then moving around the building
   while the supplicant roams between the stations installed in different
   locations within the building, and not reachable from the entrance.

   The above is a borderline case in practice, but 802.11 kind of impies
   this is the case we should be coding for. */

/* The code below is the only part of the in the supplicant itself that
   deals with IEs (Information Elements). IEs are normally parsed in the
   client.

   Ref. IEEE 802.11-2012 8.4.2 Information elements,
                         8.4.2.27 RSNE

   We also pick IEs to transmit here, which we will send with the ASSOCIATE
   command and later in EAPOL packet 3/4, confirming our choice of ciphers
   and auth scheme to the AP. */

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

/* Same of the values above must also be passed via NL to the driver
   as host-ended integers. Some drivers will rely on the IEs, some need
   the NL values, and strange things may happen if they aren't in sync */

#define RSN_CIPHER_CCMP  0x000FAC04
#define RSN_CIPHER_TKIP  0x000FAC02
#define RSN_AKM_PSK      0x000FAC02

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
   several connectable APs from the same network in both 2.4GHz and 5GHz.

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

static void set_current_bss(struct scan* sc)
{
	ap.freq = sc->freq;

	memcpy(ap.bssid, sc->bssid, MACLEN);

	ap.akm = RSN_AKM_PSK;
	ap.pairwise = RSN_CIPHER_CCMP;

	if(sc->flags & SF_TKIP) {
		ap.txies = (void*)ies_ccmp_tkip;
		ap.iesize = sizeof(ies_ccmp_tkip);
		ap.gtklen = 32;
		ap.group = RSN_CIPHER_TKIP;
	} else {
		ap.txies = (void*)ies_ccmp_ccmp;
		ap.iesize = sizeof(ies_ccmp_ccmp);
		ap.gtklen = 16;
		ap.group = RSN_CIPHER_CCMP;
	}

	sc->flags |= SF_TRIED;
}

/* We never sort the scanlist, we just pick the best non-marked AP,
   mark it and try to connect until there's no more APs left. */

static struct scan* get_best_bss(void)
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

static struct scan* find_current_bss(void)
{
	struct scan* sc;

	if(!nonzero(ap.bssid, 6))
		return NULL;

	for(sc = scans; sc < scans + nscans; sc++)
		if(!memcmp(sc->bssid, ap.bssid, 6))
			return sc;

	return NULL;
}

int pick_best_bss(void)
{
	struct scan* sc;

	if(!(sc = get_best_bss()))
		return -ENOENT;

	set_current_bss(sc);

	return 0;
}

int current_bss_in_scans(void)
{
	return !!find_current_bss();
}

void mark_current_bss_good(void)
{
	struct scan* sc;

	if(!(sc = find_current_bss()))
		return;

	sc->flags &= ~SF_TRIED;
}

void clear_all_bss_marks(void)
{
	struct scan* sc;
	int mask = ~(SF_GOOD | SF_TKIP | SF_SEEN | SF_TRIED);

	for(sc = scans; sc < scans + nscans; sc++)
		sc->flags &= mask;
}
