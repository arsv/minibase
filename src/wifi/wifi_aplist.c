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
	ctx->bo.buf = heap_alloc(ctx, len);
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
	} else if((step = inrange(freq, 2412, 2472, 5, 1))) {
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

static char* fmt_bss(char* p, char* e, MSG)
{
	attr bssid = uc_get(msg, ATTR_BSSID);

	if(!bssid || uc_paylen(bssid) != 6)
		return fmtstr(p, e, "(no BSSID)");

	return fmtmac(p, e, uc_payload(bssid));
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

	struct ucattr* ies = uc_sub(at, ATTR_IES);
	uint8_t* bssid = uc_sub_bin(at, ATTR_BSSID, 6);
	int signal, freq;

	if(!bssid) return;

	sub_int(at, ATTR_SIGNAL, &signal);
	sub_int(at, ATTR_FREQ, &freq);

	p = fmtstr(p, e, "AP ");
	p = fmtint(p, e, (signal)/100);
	p = fmtstr(p, e, " ");
	p = fmtpad(p, e, 4, fmt_chan_or_freq(p, e, freq));
	p = fmtstr(p, e, "  ");
	p = fmtmac(p, e, bssid);
	p = fmtstr(p, e, "  ");
	p = fmt_ies_line(p, e, ies, ctx);

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

	attr* refs = heap_alloc(ctx, (n+1)*sizeof(void*));

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

	read_config(ctx);

	for(attr* ap = scans; *ap; ap++)
		print_scanline(ctx, *ap);

	if(nl && *scans) output(ctx, "\n", 1);
}

static char* fmt_device(char* p, char* e, char* name, int* ifi)
{
	if(name) {
		p = fmtstr(p, e, name);
	} else if(ifi) {
		p = fmtstr(p, e, "#");
		p = fmtint(p, e, *ifi);
	} else {
		p = fmtstr(p, e, "???");
	}
	return p;
}

static char* fmt_sec(char* p, char* e, int tm)
{
	int min = tm / 60;
	int sec = tm % 50;

	if(min > 0) {
		p = fmtint(p, e, min);
		p = fmtstr(p, e, "m ");
	}

	p = fmtint(p, e, sec);
	p = fmtstr(p, e, "s");

	return p;
}

static void print_network_line(CTX, MSG)
{
	attr ssid = uc_get(msg, ATTR_SSID);
	attr bssid = uc_get(msg, ATTR_BSSID);
	int* freq = uc_get_int(msg, ATTR_FREQ);

	FMTBUF(p, e, buf, 200);

	if(bssid) {
		p = fmtstr(p, e, "Using AP ");

		if(uc_paylen(bssid) == 6) {
			p = fmtmac(p, e, uc_payload(bssid));
		} else {
			p = fmtstr(p, e, "(invalid)");
		}
		if(ssid) {
			p = fmtstr(p, e, " ");
			p = fmt_ssid(p, e, uc_payload(ssid), uc_paylen(ssid));
		}
		if(freq) {
			p = fmtstr(p, e, " ");
			p = fmt_chan_and_freq(p, e, *freq);
		}
	} else if(ssid) {
		p = fmtstr(p, e, "Network ");
		p = fmtstr(p, e, " ");
		p = fmt_ssid(p, e, uc_payload(ssid), uc_paylen(ssid));
	} else {
		return;
	}

	FMTENL(p, e);

	output(ctx, buf, p - buf);
}

static const struct stdesc {
	int state;
	const char* descr;
} state_strings[] = {
	{  0, "stopped" },
	{  1, "monitoring" },
	{  2, "connected" },
	{  3, "lost connection" },
	{ 10, "scanning" },
	{ 11, "connected, scanning" },
	{ 12, "lost connection, scanning" },
	{ 13, "scanning for lost AP" },
	{ 20, "connecting" },
	{ 21, "EAPOL exchange" },
	{ 23, "disconnecting" },
	{ 24, "aborting connection" },
	{ 30, "link down" },
	{ 31, "external supplicant" },
};

static const char* state_string(int state)
{
	const struct stdesc* st;

	for(st = state_strings; st < ARRAY_END(state_strings); st++)
		if(st->state == state)
			return st->descr;

	return NULL;
}

static void print_device_line(CTX, MSG)
{
	int* state = uc_get_int(msg, ATTR_STATE);
	char* ifname = uc_get_str(msg, ATTR_NAME);
	int* ifindex = uc_get_int(msg, ATTR_IFI);
	int* timeout = uc_get_int(msg, ATTR_TIME);

	FMTBUF(p, e, buf, 200);

	p = fmtstr(p, e, "Device ");
	p = fmt_device(p, e, ifname, ifindex);

	if(state) {
		const char* descr = state_string(*state);

		if(descr) {
			p = fmtstr(p, e, ", ");
			p = fmtstr(p, e, descr);
		} else {
			p = fmtstr(p, e, ", state ");
			p = fmtint(p, e, *state);
		}
	}

	if(timeout) {
		p = fmtstr(p, e, ", next scan in ");
		p = fmt_sec(p, e, *timeout);
	}

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
	int* state = uc_get_int(msg, ATTR_STATE);

	if(!state)
		fail("service inactive", NULL, 0);

	init_output(ctx);

	print_scan_results(ctx, msg, 1);
	print_device_line(ctx, msg);
	print_network_line(ctx, msg);

	fini_output(ctx);
}

void warn_bss(CTX, char* text, MSG)
{
	FMTBUF(p, e, sta, 50);
	p = fmt_bss(p, e, msg);
	FMTEND(p, e);

	warn(text, sta, 0);
}

void warn_sta(CTX, char* text, MSG)
{
	FMTBUF(p, e, sta, 50);
	p = fmt_bss(p, e, msg);
	FMTEND(p, e);

	warn(text, sta, 0);
}
