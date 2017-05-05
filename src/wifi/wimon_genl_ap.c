#include <netlink.h>
#include <netlink/genl/nl80211.h>

#include <format.h>
#include <string.h>

#include "wimon.h"

/* Most of the information about the stations come encoded in IEs.
   IEs are a pain to deal with and carry a lot more than we need,
   so we decode the interesting parts and drop everything else.

   What we do need is the SSID (AP name) and the ciphers it supports.

   See also: ies_ccmp, ies_tkip from wpa_netlink.c
   Here we must expect much wider range of possible IEs however,
   includning WPS and WPA1.

   Ref. IEEE 802.11-2012 8.4.2 Information elements,
                         8.4.2.27 RSNE

   It does not look like current 802.11 revisions describe vendor-extension
   IEs used with WPS and WPA1; refer to iw sources, scan.c in particular.

   Reminder: WPA = WPA1, RSN = WPA2, WPS = WEP. */

struct ies {
	uint8_t type;
	uint8_t len;
	char payload[];
};

static int get2le(char* p, char* e)
{
	if(p + 3 > e) return 0;
	return (p[0] & 0xFF)
	    | ((p[1] & 0xFF) << 8);
}

static int get4be(char* p, char* e)
{
	if(p + 4 > e) return 0;
	return ((p[0] & 0xFF) << 24)
	     | ((p[1] & 0xFF) << 16)
	     | ((p[2] & 0xFF) <<  8)
	     | ((p[3] & 0xFF));
}

static void parse_rsn_ie(struct scan* sc, int len, char* buf)
{
	char* p = buf;
	char* e = buf + len;
	int type = sc->type;

	int ver = get2le(p, e);     p += 2;

	if(ver != 1) return;

	int group = get4be(p, e);   p += 4;
	int pcnt  = get2le(p, e);   p += 2;
	char* pair = p;             p += 4*pcnt;
	int acnt  = get2le(p, e);   p += 2;
	char* akm = p;            //p += 4*acnt;

	for(p = pair; p < pair + 4*pcnt; p += 4)
		switch(get4be(p, e)) {
			case 0x000FAC02: type |= ST_RSN_P_TKIP; break;
			case 0x000FAC04: type |= ST_RSN_P_CCMP; break;
		}

	for(p = akm; p < akm + 4*acnt; p += 4)
		if(get4be(p, e) == 0x000FAC02)
			type |= ST_RSN_PSK;

	switch(group) {
		case 0x000FAC02: type |= ST_RSN_G_TKIP; break;
		case 0x000FAC04: type |= ST_RSN_G_CCMP; break;
	}

	sc->type = type;
}

static const char ms_oui[] = { 0x00, 0x50, 0xf2 };

static void parse_vendor(struct scan* sc, int len, char* buf)
{
	if(len < 4)
		return;
	if(memcmp(buf, ms_oui, 3))
		return;

	if(buf[3] == 1)
		sc->type |= ST_WPA;
	else if(buf[3] == 4)
		sc->type |= ST_WPS;
}

static void set_station_ssid(struct scan* sc, int len, char* buf)
{
	int i;

	if(len > sizeof(sc->ssid))
		len = sizeof(sc->ssid);

	memcpy(sc->ssid, buf, len);

	for(i = len; i > 0; i--)
		if(buf[i-1])
			break;

	sc->slen = i;
}

void parse_station_ies(struct scan* sc, struct nlattr* at)
{
	int len = nl_attr_len(at);
	char* buf = at->payload;
	char* end = buf + len;

	char* ptr = buf;

	while(ptr < end) {
		struct ies* ie = (struct ies*) ptr;
		int ielen = sizeof(*ie) + ie->len;

		if(ptr + ielen > end)
			break;
		if(ie->type == 0)
			set_station_ssid(sc, ie->len, ie->payload);
		else if(ie->type == 48)
			parse_rsn_ie(sc, ie->len, ie->payload);
		else if(ie->type == 221)
			parse_vendor(sc, ie->len, ie->payload);

		ptr += ielen;
	}
}

int get_i32_or_zero(struct nlattr* bss, int key)
{
	int32_t* val = nl_sub_i32(bss, key);
	return val ? *val : 0;
}

static void dump_sta(struct scan* sc)
{
	char* type;

	if(!sc->type)
		type = "---";
	else if(sc->type & (ST_RSN_PSK | ST_RSN_P_CCMP | ST_RSN_G_CCMP))
		type = "CC ";
	else if(sc->type & (ST_RSN_PSK | ST_RSN_P_CCMP | ST_RSN_G_TKIP))
		type = "CT ";
	else if(sc->type & (ST_RSN_PSK | ST_RSN_P_TKIP | ST_RSN_G_TKIP))
		type = "TT ";
	else if(sc->type & (ST_RSN))
		type = "RSN";
	else if(sc->type & (ST_WPA))
		type = "WPA";
	else if(sc->type & (ST_WPS))
		type = "WPS";
	else
		type = "???";

	eprintf("station %i %i %3s \"%s\"\n",
			sc->freq, sc->signal/100, type, sc->ssid);
}

void parse_scan_result(struct link* ls, struct nlgen* msg)
{
	struct scan* sc;
	struct nlattr* bss;
	struct nlattr* ies;
	uint8_t* bssid;

	if(!(bss = nl_get_nest(msg, NL80211_ATTR_BSS)))
		return;
	if(!(bssid = nl_sub_of_len(bss, NL80211_BSS_BSSID, 6)))
		return;
	if(!(sc = grab_scan_slot(bssid)))
		return; /* out of scan slots */

	sc->ifi = ls->ifi;
	memcpy(sc->bssid, bssid, 6);
	sc->freq = get_i32_or_zero(bss, NL80211_BSS_FREQUENCY);
	sc->signal = get_i32_or_zero(bss, NL80211_BSS_SIGNAL_MBM);
	sc->type = 0;

	if((ies = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		parse_station_ies(sc, ies);

	dump_sta(sc);
}
