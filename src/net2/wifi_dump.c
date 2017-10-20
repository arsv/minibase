#include <nlusctl.h>
#include <heap.h>
#include <util.h>
#include <string.h>
#include <format.h>
#include <output.h>

#include "common.h"
#include "wifi.h"

static const struct channel {
	short chan;
	short freq;
} channels[] = {
	{ 1,	2412 },
	{ 2,	2417 },
	{ 3,	2422 },
	{ 4,	2427 },
	{ 5,	2432 },
	{ 6,	2437 },
	{ 7,	2442 },
	{ 8,	2447 },
	{ 9,	2452 },
	{ 10,	2457 },
	{ 11,	2462 },
	{ 12,	2467 },
	{ 13,	2472 },
	{ 14,	2484 },
	{ 7,	5035 },
	{ 8,	5040 },
	{ 9,	5045 },
	{ 11,	5055 },
	{ 12,	5060 },
	{ 16,	5080 },
	{ 34,	5170 },
	{ 36,	5180 },
	{ 38,	5190 },
	{ 40,	5200 },
	{ 42,	5210 },
	{ 44,	5220 },
	{ 46,	5230 },
	{ 48,	5240 },
	{ 50,	5250 },
	{ 52,	5260 },
	{ 54,	5270 },
	{ 56,	5280 },
	{ 58,	5290 },
	{ 60,	5300 },
	{ 62,	5310 },
	{ 64,	5320 },
	{ 100,	5500 },
	{ 102,	5510 },
	{ 104,	5520 },
	{ 106,	5530 },
	{ 108,	5540 },
	{ 110,	5550 },
	{ 112,	5560 },
	{ 114,	5570 },
	{ 116,	5580 },
	{ 118,	5590 },
	{ 120,	5600 },
	{ 122,	5610 },
	{ 124,	5620 },
	{ 126,	5630 },
	{ 128,	5640 },
	{ 132,	5660 },
	{ 134,	5670 },
	{ 136,	5680 },
	{ 138,	5690 },
	{ 140,	5700 },
	{ 142,	5710 },
	{ 144,	5720 },
	{ 149,	5745 },
	{ 151,	5755 },
	{ 153,	5765 },
	{ 155,	5775 },
	{ 157,	5785 },
	{ 159,	5795 },
	{ 161,	5805 },
	{ 165,	5825 },
	{ 169,	5845 },
	{ 173,	5865 },
	{ 183,	4915 },
	{ 184,	4920 },
	{ 185,	4925 },
	{ 187,	4935 },
	{ 188,	4940 },
	{ 189,	4945 },
	{ 192,	4960 },
	{ 196,	4980 },
};

static int get_channel(int freq)
{
	const struct channel* p;

	for(p = channels; p < ARRAY_END(channels); p++)
		if(p->freq == freq)
			return p->chan;

	return -1;
}

#define DICTEND -1

static const struct dict {
	int val;
	char name[16];
} wistates[] = {
	{ WS_IDLE,       "Idle"       },
	{ WS_RFKILLED,   "RF-kill"    },
	{ WS_NETDOWN,    "Net down"   },
	{ WS_SCANNING,   "Scanning"   },
	{ WS_CONNECTING, "Connecting" },
	{ WS_CONNECTED,  "Connected"  },
	{ DICTEND,       ""           }
};

static char* fmt_kv(char* p, char* e, int val, const struct dict* dc)
{
	const struct dict* kv;

	for(kv = dc; kv->val != DICTEND; kv++)
		if(kv->val == val)
			break;

	if(kv->val == DICTEND)
		return p;

	p = fmtstr(p, e, kv->name);

	return p;
}

static int cmp_int(attr at, attr bt, int key)
{
	int* na = uc_sub_int(at, key);
	int* nb = uc_sub_int(bt, key);

	if(!na && nb)
		return -1;
	if(na && !nb)
		return  1;
	if(!na || !nb)
		return 0;
	if(*na < *nb)
		return -1;
	if(*na > *nb)
		return  1;

	return 0;
}

static int scan_ord(const void* a, const void* b)
{
	attr at = *((attr*)a);
	attr bt = *((attr*)b);
	int ret;

	if((ret = cmp_int(at, bt, ATTR_SIGNAL)))
		return -ret;
	if((ret = cmp_int(at, bt, ATTR_FREQ)))
		return ret;

	return 0;
}

static char* fmt_ssid(char* p, char* e, attr at)
{
	int i;

	uint8_t* ssid = uc_payload(at);
	int slen = uc_paylen(at);

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

static char* fmt_wifi_bssid(char* p, char* e, attr at)
{
	if(!at || uc_paylen(at) != 6)
		return p;

	p = fmtstr(p, e, " ");
	p = fmtmac(p, e, uc_payload(at));

	return p;
}

static char* fmt_wifi_ssid(char* p, char* e, attr ssid)
{
	if(!ssid)
		goto out;

	p = fmtstr(p, e, "AP ");
	p = fmt_ssid(p, e, ssid);
out:
	return p;
}

static void get_int(MSG, int attr, int* val)
{
	int* p;

	if((p = uc_get_int(msg, attr)))
		*val = *p;
	else
		*val = 0;
}

static char* fmt_station(char* p, char* e, MSG)
{
	int freq, chan;

	get_int(msg, ATTR_FREQ, &freq);
	chan = get_channel(freq);

	p = fmt_wifi_ssid(p, e, uc_get(msg, ATTR_SSID));
	p = fmt_wifi_bssid(p, e, uc_get(msg, ATTR_BSSID));

	if(freq) {
		p = fmtstr(p, e, " (");
		if(chan > 0) {
			p = fmtstr(p, e, "");
			p = fmtint(p, e, chan);
			p = fmtstr(p, e, freq > 5000 ? "a" : "b");
			p = fmtstr(p, e, "/");
		}
		p = fmtint(p, e, freq);
		p = fmtstr(p, e, "MHz)");
	}

	return p;
}

static char* fmt_freq(char* p, char* e, int freq)
{
	int chan = get_channel(freq);

	if(chan <= 0)
		return fmtint(p, e, freq);

	p = fmtint(p, e, chan);
	p = fmtstr(p, e, freq > 5000 ? "a" : "b");

	return p;
}

static void sub_int(AT, int attr, int* val)
{
	int* p;

	if((p = uc_sub_int(at, attr)))
		*val = *p;
	else
		*val = 0;
}

static void print_scanline(CTX, AT)
{
	char buf[200];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	struct ucattr* ssid = uc_sub(at, ATTR_SSID);
	uint8_t* bssid = uc_sub_bin(at, ATTR_BSSID, 6);
	struct ucattr* prio = uc_sub(at, ATTR_PRIO);
	int signal, freq;

	if(!bssid || !ssid) return;

	sub_int(at, ATTR_SIGNAL, &signal);
	sub_int(at, ATTR_FREQ, &freq);

	p = fmtstr(p, e, "AP ");
	p = fmtint(p, e, (signal)/100);
	p = fmtstr(p, e, " ");
	p = fmtpad(p, e, 4, fmt_freq(p, e, freq));
	p = fmtstr(p, e, "  ");
	p = fmtmac(p, e, bssid);
	p = fmtstr(p, e, "  ");
	p = fmt_ssid(p, e, ssid);
	if(prio) p = fmtstr(p, e, " *");
	*p++ = '\n';

	output(ctx, buf, p - buf);
}

static attr* prep_list(CTX, MSG, int key, qcmp2 cmp)
{
	int n = 0, i = 0;
	attr at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(at->key == key)
			n++;

	attr* refs = halloc(&ctx->hp, (n+1)*sizeof(void*));

	for(at = uc_get_0(msg); at && i < n; at = uc_get_n(msg, at))
		if(at->key == key)
			refs[i++] = at;
	refs[i] = NULL;

	qsort(refs, i, sizeof(void*), cmp);

	return refs;
}

static void print_scan_results(CTX, MSG, int nl)
{
	attr* scans = prep_list(ctx, msg, ATTR_SCAN, scan_ord);

	for(attr* ap = scans; *ap; ap++)
		print_scanline(ctx, *ap);

	if(nl && *scans) output(ctx, "\n", 1);
}

static void print_status(CTX, MSG)
{
	int state;

	FMTBUF(p, e, buf, 200);

	get_int(msg, ATTR_STATE, &state);
	p = fmt_kv(p, e, state, wistates);
	p = fmtstr(p, e, " ");
	p = fmt_station(p, e, msg);

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

void dump_scanlist(CTX, MSG)
{
	init_output(ctx);
	print_scan_results(ctx, msg, 0);
	fini_output(ctx);
}

void dump_status(CTX, MSG)
{
	init_output(ctx);

	print_scan_results(ctx, msg, 1);

	print_status(ctx, msg);

	fini_output(ctx);
}

void warn_sta(char* text, MSG)
{
	FMTBUF(p, e, sta, 50);
	p = fmt_station(p, e, msg);
	FMTEND(p, e);

	warn(text, sta, 0);
}
