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

static const char ms_oui[] = { 0x00, 0x50, 0xf2 };

static void parse_vendor_wpa(struct scan* sc, int len, char* buf)
{
	sc->type |= ST_WPA2_CC; /* XXX */
}

static void parse_vendor_wps(struct scan* sc, int len, char* buf)
{
}

static void parse_vendor_ie(struct scan* sc, int len, char* buf)
{
	if(len < 4)
		return;
	if(memcmp(buf, ms_oui, sizeof(ms_oui)))
		return;

	char* data = buf + 4;
	int dlen = len - 4;

	switch(buf[3]) {
		case 1: return parse_vendor_wpa(sc, dlen, data);
		case 4: return parse_vendor_wps(sc, dlen, data);
	}
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
		else if(ie->type == 221)
			parse_vendor_ie(sc, ie->len, ie->payload);

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

	if((ies = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		parse_station_ies(sc, ies);

	eprintf("station %i %i \"%s\"\n", sc->freq, sc->signal/100, sc->ssid);

	//nl_dump_genl(&msg->nlm);


}
