#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/genl/nl80211.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "wimon.h"

/* Some station data comes in binary blobs called "IES",
   which are very similar to nested NL attributes but use
   a slightly different header. Apparently these come from
   the device itself. */

struct ies {
	uint8_t type;
	uint8_t len;
	char payload[];
};

struct ies* get_ies_of_type(struct nlattr* at, int type)
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
			return ie;

		ptr += ielen;
	}

	return NULL;
}

void parse_station_ies(struct scan* sc, struct nlattr* bss)
{
	struct nlattr* at;
	struct ies* ie;

	if(!(at = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		return;
	if(!(ie = get_ies_of_type(at, 0)))
		return;

	int len = ie->len;
	char* ssid = ie->payload;

	if(len > sizeof(sc->ssid))
		len = sizeof(sc->ssid);

	memcpy(sc->ssid, ssid, len);
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
	uint8_t* bssid;

	if(!(bss = nl_get_nest(msg, NL80211_ATTR_BSS)))
		return;
	if(!(bssid = nl_sub_of_len(bss, NL80211_BSS_BSSID, 6)))
		return;
	if(!(sc = grab_scan_slot(ls->ifi, bssid)))
		return;

	sc->ifi = ls->ifi;
	sc->seq = ls->seq;
	memcpy(sc->bssid, bssid, 6);
	sc->freq = get_i32_or_zero(bss, NL80211_BSS_FREQUENCY);
	sc->signal = get_i32_or_zero(bss, NL80211_BSS_SIGNAL_MBM);

	parse_station_ies(sc, bss);

	eprintf("station %i %i \"%s\"\n", sc->freq, sc->signal/100, sc->ssid);

	//nl_dump_genl(&msg->nlm);


}
