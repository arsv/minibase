#include <nlusctl.h>
#include <heap.h>
#include <util.h>
#include <format.h>
#include <string.h>

#include "common.h"
#include "wictl.h"

static int cmp_int(struct ucattr* at, struct ucattr* bt, int key)
{
	uint32_t* na = uc_sub_u32(at, key);
	uint32_t* nb = uc_sub_u32(bt, key);

	if(!na || !nb)
		return 0;
	if(*na < *nb)
		return -1;
	if(*na > *nb)
		return  1;

	return 0;
}

static int scan_ord(const void* a, const void* b, long p)
{
	struct ucattr* at = *((struct ucattr**)a);
	struct ucattr* bt = *((struct ucattr**)b);
	int ret;

	if((ret = cmp_int(at, bt, ATTR_SIGNAL)))
		return ret;
	if((ret = cmp_int(at, bt, ATTR_FREQ)))
		return -ret;

	return 0;
}

static struct ucattr** prep_scan_list(struct top* ctx, struct ucmsg* msg)
{
	int n = 0, i = 0;
	struct ucattr* at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(at->key == ATTR_SCAN)
			n++;

	struct ucattr** refs = halloc(&ctx->hp, (n+1)*sizeof(void*));

	for(at = uc_get_0(msg); at && i < n; at = uc_get_n(msg, at))
		if(at->key == ATTR_SCAN)
			refs[i++] = at;
	refs[i] = NULL;

	qsort(refs, i, sizeof(void*), scan_ord, 0);

	return refs;
}

static void dump_scan_line(struct ucattr* sc)
{
	struct ucattr* ssid = uc_sub_k(sc, ATTR_SSID);
	uint8_t* bssid = (uint8_t*)uc_sub_bin(sc, ATTR_BSSID, 6);
	uint32_t* freq = uc_sub_u32(sc, ATTR_FREQ);
	int32_t* signal = uc_sub_i32(sc, ATTR_SIGNAL);

	int slen = ssid->len - sizeof(*ssid);
	char sbuf[slen+1];

	memcpy(sbuf, ssid->payload, slen);
	sbuf[slen] = '\0';

	eprintf("AP %i %i %02X:%02X:%02X:%02X:%02X:%02X %s\n",
			(*signal)/100, *freq,
			bssid[0], bssid[1], bssid[2],
			bssid[3], bssid[4], bssid[5],
			sbuf);
}

void dump_status(struct top* ctx, struct ucmsg* msg)
{
	struct ucattr** scan = prep_scan_list(ctx, msg);
	struct ucattr** sc;

	for(sc = scan; *sc; sc++)
		dump_scan_line(*sc);
}

