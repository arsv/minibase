#include <nlusctl.h>
#include <heap.h>
#include <util.h>
#include <string.h>
#include <format.h>
#include <output.h>

#include "common.h"
#include "wifi.h"

static void init_output(CTX)
{
	int len = 2048;

	ctx->bo.fd = STDOUT;
	ctx->bo.buf = halloc(&ctx->hp, len);
	ctx->bo.len = len;
	ctx->bo.ptr = 0;
}

static void fini_output(CTX)
{
	bufoutflush(&ctx->bo);
}

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

/* Wi-Fi channel designation.
   
   Ref. https://en.wikipedia.org/wiki/List_of_WLAN_channels
 
   Bands a and b refer to 802.11a and 802.11b respectively. */

static int inrange(int freq, int a, int b, int s, int i)
{
	if(freq < a)
		return 0;
	if(freq > b)
		return 0;

	int d = freq - a;

	if(d % s)
		return 0;

	return i + (d/s);
}

static char* fmt_chan(char* p, char* e, int freq)
{
	int step;
	char band;

	if(freq == 2484) {
		step = 14;
		band = 'b';
	} else if((step = inrange(freq, 2412, 2467, 5, 1))) {
		band = 'b';
	} else if((step = inrange(freq, 5035, 5865, 5, 7))) {
		band = 'a';
	} else if((step = inrange(freq, 4915, 4980, 5, 183))) {
		band = 'a';
	} else {
		return p;
	}

	p = fmtint(p, e, step);
	p = fmtchar(p, e, band);

	return p;
}

/* 5240 -> "48a", 5241 -> "5241" */

static char* fmt_chan_or_freq(char* p, char* e, int freq)
{
	char* s = p;

	p = fmt_chan(p, e, freq);

	if(p > s) return p;

	p = fmtint(p, e, freq);

	return p;
}

/* 5240 -> "48a/5240MHz", 5241 -> "5241MHz" */

static char* fmt_chan_and_freq(char* p, char* e, int freq)
{
	char* s = p;

	p = fmt_chan(p, e, freq);

	if(p > s) p = fmtchar(p, e, '/');

	p = fmtint(p, e, freq);
	p = fmtstr(p, e, "MHz");

	return p;
}

#define DICTEND -1

static const struct dict {
	int val;
	char name[16];
} wistates[] = {
	{ WS_IDLE,       "Idle"       },
	{ WS_RFKILLED,   "RF-kill"    },
	{ WS_NETDOWN,    "Net down"   },
	{ WS_EXTERNAL,   "External"   },
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
	int freq;

	get_int(msg, ATTR_FREQ, &freq);

	p = fmt_wifi_ssid(p, e, uc_get(msg, ATTR_SSID));
	p = fmt_wifi_bssid(p, e, uc_get(msg, ATTR_BSSID));

	if(freq) {
		p = fmtstr(p, e, " (");
		p = fmt_chan_and_freq(p, e, freq);
		p = fmtstr(p, e, ")");
	}

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
	p = fmtpad(p, e, 4, fmt_chan_or_freq(p, e, freq));
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
