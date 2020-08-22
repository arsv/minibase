#include <nlusctl.h>
#include <util.h>
#include <string.h>
#include <format.h>
#include <output.h>

#include "common.h"
#include "wifi.h"

/* See wifi_aplist.c for more info on IEs.

   There, parsing is kept to the minimum necessary to tell if the AP
   is connectable. Here in the client, our goal is to tell the user
   what kind of AP it is.

   Simplified implementation for now, to be extended. */

#define C_RSN     (1<<0)
#define C_PSK     (1<<1)
#define C_TKIP_P  (1<<8)
#define C_CCMP_P  (1<<9)
#define C_CCMP_G  (1<<16)
#define C_TKIP_G  (1<<17)
#define C_WPA     (1<<24)
#define C_WPS     (1<<25)

struct ies {
	byte type;
	byte len;
	byte payload[];
};

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

static int tick_rsn_caps(int caps, byte* buf, int len)
{
	byte* p = buf;
	byte* e = buf + len;

	int ver = get2le(p, e);     p += 2;

	if(ver != 1) return caps;

	caps |= C_RSN;

	int group = get4be(p, e);   p += 4;
	int pcnt  = get2le(p, e);   p += 2;
	byte* pair = p;             p += 4*pcnt;
	int acnt  = get2le(p, e);   p += 2;
	byte* akm = p;            //p += 4*acnt;

	for(p = pair; p < pair + 4*pcnt; p += 4)
		switch(get4be(p, e)) {
			case 0x000FAC02: caps |= C_TKIP_P; break;
			case 0x000FAC04: caps |= C_CCMP_P; break;
		}

	for(p = akm; p < akm + 4*acnt; p += 4)
		if(get4be(p, e) == 0x000FAC02)
			caps |= C_PSK;

	switch(group) {
		case 0x000FAC02: caps |= C_TKIP_G; break;
		case 0x000FAC04: caps |= C_CCMP_G; break;
	}

	return caps;
}

static const char ms_oui[] = { 0x00, 0x50, 0xf2 };

static int tick_vendor(int caps, byte* buf, int len)
{
	if(len < 4)
		goto out;
	if(memcmp(buf, ms_oui, 3))
		goto out;

	if(buf[3] == 1)
		caps |= C_WPA;
	else if(buf[3] == 4)
		caps |= C_WPS;
out:
	return caps;
}

static int allbits(int val, int mask)
{
	return ((val & mask) == mask);
}

/* This shows up in scan lines. Our goal here is to give the user
   some indication that the AP is not connectable, nothing more. */

static const char* access_tag(void* buf, int len)
{
	int caps = 0;

	void* ptr = buf;
	void* end = buf + len;

	while(ptr < end) {
		struct ies* ie = ptr;
		int ielen = sizeof(*ie) + ie->len;

		if(ptr + ielen > end)
			break;
		if(ie->type == 48)
			caps = tick_rsn_caps(caps, ie->payload, ie->len);
		if(ie->type == 221)
			caps = tick_vendor(caps, ie->payload, ie->len);

		ptr += ielen;
	}

	if(allbits(caps, C_RSN | C_PSK | C_CCMP_P | C_CCMP_G))
		return "2CC";
	if(allbits(caps, C_RSN | C_PSK | C_CCMP_P | C_TKIP_G))
		return "2CT";
	if(allbits(caps, C_RSN | C_PSK | C_TKIP_P | C_TKIP_G))
		return "2TT";

	if(allbits(caps, C_RSN | C_PSK))
		return "2??";
	else if(caps & C_RSN)
		return "2xP";
	else if(caps & C_WPA)
		return "WP1";
	else if(caps & C_WPS)
		return "WEP";

	return "";
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

char* fmt_ssid(char* p, char* e, byte* ssid, int slen)
{
	int i;

	for(i = 0; i < slen; i++) {
		if(ssid[i] >= 0x20) {
			p = fmtchar(p, e, ssid[i]);
		} else {
			p = fmtstr(p, e, "\\x");
			p = fmtbyte(p, e, ssid[i]);
		}
	}

	return p;
}

char* fmt_ies_line(char* p, char* e, struct ucattr* at, CTX)
{
	struct ies* ie;

	if(!at)
		return fmtstr(p, e, "(no IEs)");

	void* buf = uc_payload(at);
	int len = uc_paylen(at);

	p = fmtpadr(p, e, 5, fmtstr(p, e, access_tag(buf, len)));

	if((ie = find_ie(buf, len, 0)))
		p = fmt_ssid(p, e, ie->payload, ie->len);
	else
		p = fmtstr(p, e, "(no SSID)");

	if(ie && check_entry(ctx, ie->payload, ie->len))
		p = fmtstr(p, e, " *");

	return p;
}

int can_use_ap(CTX, struct ucattr* ies)
{
	struct ies* ie;

	void* buf = uc_payload(ies);
	int len = uc_paylen(ies);

	if(!(ie = find_ie(buf, len, 0)))
		return AP_BADSSID;
	if(ie->len != ctx->slen)
		return AP_BADSSID;
	if(memcmp(ie->payload, ctx->ssid, ctx->slen))
		return AP_BADSSID;

	if(!(ie = find_ie(buf, len, 48)))
		return AP_NOCRYPT;

	int caps = tick_rsn_caps(0, ie->payload, ie->len);

	if(allbits(caps, C_RSN | C_PSK | C_CCMP_P | C_CCMP_G))
		return AP_CANCONN;
	if(allbits(caps, C_RSN | C_PSK | C_CCMP_P | C_TKIP_G))
		return AP_CANCONN;

	return AP_NOCRYPT;
}
