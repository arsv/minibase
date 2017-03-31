#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/genl/nl80211.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "wimon.h"
#include "wimon_slot.h"

/* Some station data comes in binary blobs called "IES",
   which are very similar to nested NL attributes but use
   a slightly different header. Apparently these come from
   the device itself. */

struct ies {
	uint8_t type;
	uint8_t len;
	char payload[];
};

/* See also: ies_ccmp, ies_tkip; sadly the structure is somewhat
   variable, so we can't easily describe it in a struct. */

static char* get2le(char* p, char* e, uint16_t* v)
{
	char* q = p; p += 2;

	if(p <= e)
		*v = (q[0] & 0xFF)
		  | ((q[1] & 0xFF) << 8);

	return p;
}

static char* get4be(char* p, char* e, uint32_t* v)
{
	char* q = p; p += 4;

	if(p <= e)
		*v = ((q[0] & 0xFF) << 24)
		   | ((q[1] & 0xFF) << 16)
		   | ((q[2] & 0xFF) <<  8)
		   | ((q[3] & 0xFF));

	return p;
}

static char* getlist(char* p, char* e, int n, uint32_t** ptr)
{
	char* q = p; p += 4*n;

	if(p <= e) *ptr = (uint32_t*)q;

	return p;
}

static int inlist(uint32_t* list, int n, int val)
{
	int i;
	uint32_t v;

	for(i = 0; i < n; i++) {
		char* q = (char*)(&list[i]);
		v = ((q[0] & 0xFF) << 24)
		  | ((q[1] & 0xFF) << 16)
		  | ((q[2] & 0xFF) <<  8)
		  | ((q[3] & 0xFF));
		if(v == val) return 1;
	}

	return 0;
}

static void parse_rsn_ie(struct scan* sc, int len, char* buf)
{
	uint16_t ver, pcnt, acnt;
	uint32_t group, *pair, *akm;

	char* p = buf;
	char* e = buf + len;

	p = get2le(p, e, &ver);

	if(ver != 1) return;

	p = get4be(p, e, &group);
	p = get2le(p, e, &pcnt);
	p = getlist(p, e, pcnt, &pair);
	p = get2le(p, e, &acnt);
	p = getlist(p, e, acnt, &akm);

	if(!inlist(akm, acnt, 0x000FAC02)) /* RSN-PSK */
		return;
	if(!inlist(pair, pcnt, 0x000FAC04)) /* CCMP */
		return;

	if(group == 0x000FAC02)
		sc->type = ST_WPA2_CT;
	if(group == 0x000FAC04)
		sc->type = ST_WPA2_CC;
}

static void set_station_ssid(struct scan* sc, int len, char* buf)
{
	if(len > sizeof(sc->ssid))
		len = sizeof(sc->ssid);

	memcpy(sc->ssid, buf, len);
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

		ptr += ielen;
	}
}

int get_i32_or_zero(struct nlattr* bss, int key)
{
	int32_t* val = nl_sub_i32(bss, key);
	return val ? *val : 0;
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
	if(!(sc = grab_scan_slot(bssid))) {
		eprintf("out of scan slots\n");
		return;
	}

	sc->ifi = ls->ifi;
	memcpy(sc->bssid, bssid, 6);
	sc->freq = get_i32_or_zero(bss, NL80211_BSS_FREQUENCY);
	sc->signal = get_i32_or_zero(bss, NL80211_BSS_SIGNAL_MBM);
	sc->type = ST_UNKNOWN;

	if((ies = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		parse_station_ies(sc, ies);

	char* type;

	if(sc->type == ST_UNKNOWN)
		type = "???";
	else if(sc->type == ST_WPA2_CC)
		type = "cc";
	else if(sc->type == ST_WPA2_CT)
		type = "ct";
	else
		type = "?!!";

	eprintf("station %i %i \"%s\" %s\n", sc->freq, sc->signal/100, sc->ssid, type);
}
