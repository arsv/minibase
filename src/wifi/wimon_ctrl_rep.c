#include <sys/file.h>
#include <sys/mman.h>

#include <format.h>
#include <nlusctl.h>
#include <string.h>
#include <heap.h>
#include <util.h>

#include "common.h"
#include "wimon.h"

/* Formatting and sending responses to wictl has little to do with the rest
   of wimon_ctrl, so it's been split.

   Long messages (i.e. anything that's not just errno) are prepared in a heap
   buffer. Timeouts are handled in _ctrl and not here. */

static void send_reply(struct conn* cn, struct ucbuf* uc)
{
	writeall(cn->fd, uc->brk, uc->ptr - uc->brk);
}

void reply(struct conn* cn, int err)
{
	char cbuf[16];
	struct ucbuf uc;

	uc_buf_set(&uc, cbuf, sizeof(cbuf));
	uc_put_hdr(&uc, err);
	uc_put_end(&uc);

	send_reply(cn, &uc);
}

static int estimate_scalist(void)
{
	int scansp = nscans*(sizeof(struct scan) + 10*sizeof(struct ucattr));

	return scansp + 128;
}

static int estimate_status(void)
{
	int scansp = nscans*(sizeof(struct scan) + 10*sizeof(struct ucattr));
	int linksp = nlinks*(sizeof(struct link) + 10*sizeof(struct ucattr));

	return scansp + linksp + 128;
}

static void prep_heap(struct heap* hp, int size)
{
	hp->brk = (void*)sys_brk(NULL);

	size += (PAGE - size % PAGE) % PAGE;

	hp->ptr = hp->brk;
	hp->end = (void*)sys_brk(hp->brk + size);
}

static void free_heap(struct heap* hp)
{
	sys_brk(hp->brk);
}

static void put_status_wifi(struct ucbuf* uc)
{
	struct ucattr* nn;
	struct link* ls;

	if(!wifi.mode && !wifi.state)
		return;
	if(!(ls = find_link_slot(wifi.ifi)))
		return;

	nn = uc_put_nest(uc, ATTR_WIFI);
	uc_put_int(uc, ATTR_IFI, wifi.ifi);
	uc_put_str(uc, ATTR_NAME, ls->name);
	uc_put_int(uc, ATTR_STATE, wifi.state);

	if(wifi.slen)
		uc_put_bin(uc, ATTR_SSID, wifi.ssid, wifi.slen);
	if(nonzero(wifi.bssid, sizeof(wifi.bssid)))
		uc_put_bin(uc, ATTR_BSSID, wifi.bssid, sizeof(wifi.bssid));
	if(wifi.freq)
		uc_put_int(uc, ATTR_FREQ, wifi.freq);

	uc_end_nest(uc, nn);
}

static int common_link_type(struct link* ls)
{
	if(ls->flags & S_NL80211)
		return LINK_NL80211;
	else
		return LINK_GENERIC;
}

static int common_link_state(struct link* ls)
{
	int state = ls->state;
	int flags = ls->flags;

	if(state == LS_ACTIVE)
		return LINK_ACTIVE;
	if(state == LS_STARTING)
		return LINK_STARTING;
	if(state == LS_STOPPING)
		return LINK_STOPPING;
	if(flags & S_CARRIER)
		return LINK_CARRIER;
	if(flags & S_ENABLED)
		return LINK_ENABLED;

	return LINK_OFF;
}

static void put_link_addrs(struct ucbuf* uc, struct link* ls)
{
	struct addr* ad = NULL;

	uc_put_int(uc, ATTR_IFI, ls->ifi);
	uc_put_str(uc, ATTR_NAME, ls->name);

	while((ad = get_addr(ls->ifi, ADDR_IFACE, ad))) {
		uint8_t buf[8];

		memcpy(buf, ad->ip, 4);
		buf[4] = ad->mask;

		uc_put_bin(uc, ATTR_IPMASK, buf, 5);
	}
}

static void put_link_routes(struct ucbuf* uc, struct link* ls)
{
	struct addr* ad = NULL;

	while((ad = get_addr(ls->ifi, ADDR_UPLINK, ad))) {
		if(nonzero(ad->ip, 4))
			uc_put_bin(uc, ATTR_UPLINK, ad->ip, 4);
		else
			uc_put_flag(uc, ATTR_UPLINK);
	}
}

static void put_status_links(struct ucbuf* uc)
{
	struct link* ls;
	struct ucattr* nn;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi) continue;

		nn = uc_put_nest(uc, ATTR_LINK);

		uc_put_int(uc, ATTR_IFI,   ls->ifi);
		uc_put_str(uc, ATTR_NAME,  ls->name);
		uc_put_int(uc, ATTR_STATE, common_link_state(ls));
		uc_put_int(uc, ATTR_TYPE,  common_link_type(ls));
		put_link_addrs(uc, ls);
		put_link_routes(uc, ls);

		uc_end_nest(uc, nn);
	}
}

static void put_status_scans(struct ucbuf* uc)
{
	struct scan* sc;
	struct ucattr* nn;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq) continue;
		nn = uc_put_nest(uc, ATTR_SCAN);
		uc_put_int(uc, ATTR_FREQ,   sc->freq);
		uc_put_int(uc, ATTR_TYPE,   sc->type);
		uc_put_int(uc, ATTR_SIGNAL, sc->signal);
		uc_put_int(uc, ATTR_PRIO,   sc->prio);
		uc_put_bin(uc, ATTR_BSSID, sc->bssid, sizeof(sc->bssid));
		uc_put_bin(uc, ATTR_SSID,  sc->ssid, sc->slen);
		uc_end_nest(uc, nn);
	}
}

void rep_status(struct conn* cn)
{
	struct heap hp;
	struct ucbuf uc;

	prep_heap(&hp, estimate_status());

	uc_buf_set(&uc, hp.brk, hp.end - hp.brk);
	uc_put_hdr(&uc, 0);
	put_status_links(&uc);
	put_status_wifi(&uc);
	put_status_scans(&uc);
	uc_put_end(&uc);

	send_reply(cn, &uc);

	free_heap(&hp);
}

void rep_scanlist(struct conn* cn)
{
	struct heap hp;
	struct ucbuf uc;

	prep_heap(&hp, estimate_scalist());

	uc_buf_set(&uc, hp.brk, hp.end - hp.brk);
	uc_put_hdr(&uc, 0);
	put_status_scans(&uc);
	uc_put_end(&uc);

	send_reply(cn, &uc);

	free_heap(&hp);
}

static struct link* get_latched_link(struct conn* cn)
{
	int ifi;

	if(cn->ifi == WIFI)
		ifi = wifi.ifi;
	else
		ifi = cn->ifi;

	return find_link_slot(ifi);
}

static void put_wifi_conf(struct ucbuf* uc)
{
	if(wifi.state != WS_CONNECTED)
		return;

	uc_put_int(uc, ATTR_FREQ, wifi.freq);
	uc_put_bin(uc, ATTR_SSID, wifi.ssid, wifi.slen);
	uc_put_bin(uc, ATTR_BSSID, wifi.bssid, sizeof(wifi.bssid));
}

void rep_linkconf(struct conn* cn)
{
	char buf[200];
	struct ucbuf uc;
	struct link* ls;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, 0);

	if((ls = get_latched_link(cn)))
		put_link_addrs(&uc, ls);
	if(cn->ifi == WIFI)
		put_wifi_conf(&uc);

	uc_put_end(&uc);

	send_reply(cn, &uc);
}
