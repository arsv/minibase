#include <sys/setsockopt.h>

#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <fail.h>

#include "nlfam.h"
#include "wimon.h"

/* NETLINK_GENERIC connection is used mostly request and fetch scan results,
   but also to track 802.11 stack state (like which devices support it).

   RTNL and GENL are two different sockets. No ordering of packets between
   them should be assumed. See comments around link_nl80211.

   In nl80211, there's a distinction between "phy" devices representing
   radios, and netdevs bound to phy-s. This part is not tracked here,
   at least for now. Wimon only deals with netdevs. Something else must
   be used to configure phy-net relations. Implementing ad-hoc or mesh
   networks may or may not require changes in this part. Likely anything
   that can be done with "iw dev" will be ok but "iw phy" will require
   phy tracking.

   There used to be a possibility to track scans from multiple devices here,
   but it has been removed since. While not particularly difficult to implement,
   it adds a lot of confusion (as in, what to do with sc->ifi) and the chances
   of multiscans being actually useful are close to zero. Current implementation
   only tracks the last scan requested, and silently ignores anything else. */

struct netlink genl;

char genl_tx[512];
char genl_rx[4*4096];
int nl80211;

/* Can't request two dumps at the same time. See RTNL on this.
   A bit different handling here, there's one possible scan dump
   and also wifi list dump which only ever gets run once. */

#define DUMP_NONE 0
#define DUMP_LINK 1
#define DUMP_SCAN 2

int genl_scan_ifi;
int genl_scan_seq;
int genl_dump_state;
int genl_scan_ready;

static void genl_send(void)
{
	if(nl_send(&genl))
		fail("send", "genl", genl.err);
}

static void genl_send_dump(void)
{
	if(nl_send_dump(&genl))
		fail("send", "genl", genl.err);
}

void trigger_scan(int ifi, int freq)
{
	struct nlattr* at;

	nl_new_cmd(&genl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u64(&genl, NL80211_ATTR_IFINDEX, ifi);

	if(freq) {
		at = nl_put_nest(&genl, NL80211_ATTR_SCAN_FREQUENCIES);
		nl_put_u32(&genl, 0, freq);
		nl_end_nest(&genl, at);
	}

	genl_send();

	genl_scan_seq = genl.seq;
	genl_scan_ifi = ifi;
}

void trigger_disconnect(int ifi)
{
	nl_new_cmd(&genl, nl80211, NL80211_CMD_DISCONNECT, 0);
	nl_put_u64(&genl, NL80211_ATTR_IFINDEX, ifi);
	genl_send();
}

static void request_scan_results(void)
{
	if(genl_dump_state) {
		genl_scan_ready = 1;
		return;
	}

	nl_new_cmd(&genl, nl80211, NL80211_CMD_GET_SCAN, 0);
	nl_put_u64(&genl, NL80211_ATTR_IFINDEX, genl_scan_ifi);
	genl_send_dump();

	genl_scan_seq = genl.seq;
	genl_scan_ready = 0;
	genl_dump_state = DUMP_SCAN;
}

static void request_wifi_list(void)
{
	/* no need to check genl_dump_lock, this is the first
	   request issued during initialization. */

	nl_new_cmd(&genl, nl80211, NL80211_CMD_GET_INTERFACE, 0);
	genl_send_dump();

	genl_dump_state = DUMP_LINK;
}

static int get_genl_ifindex(struct nlgen* msg)
{
	uint32_t* ifi = nl_get_u32(msg, NL80211_ATTR_IFINDEX);
	return ifi ? *ifi : 0;
}

static void msg_new_wifi(struct link* ls, struct nlgen* msg)
{
	ls->flags |= S_NL80211;

	wifi_ready(ls);
}

static void msg_new_wifi_early(int ifi, struct nlgen* msg)
{
	struct link* ls = grab_link_slot(ifi);

	ls->ifi = ifi;
	ls->flags |= S_NL80211;

	/* and do nothing until RTNL code calls link_new */
}

static void msg_del_wifi(struct link* ls, struct nlgen* msg)
{
	ls->flags &= ~S_NL80211;

	if(ls->ifi != genl_scan_ifi)
		return;

	drop_scan_slots();
}

static void msg_scan_start(struct link* ls, struct nlgen* msg)
{
	if(ls->ifi != genl_scan_ifi)
		return;
}

static void msg_scan_abort(struct link* ls, struct nlgen* msg)
{
	if(ls->ifi != genl_scan_ifi)
		return;

	wifi_scan_fail(-EINTR);
}

static void mark_stale_scan_slots(struct nlgen* msg)
{
	struct nlattr* at;
	struct nlattr* sb;
	uint32_t* fq;
	struct scan* sc;

	if(!(at = nl_get_nest(msg, NL80211_ATTR_SCAN_FREQUENCIES)))
		return;

	for(sc = scans; sc < scans + nscans; sc++) {
		if(!sc->freq)
			continue;
		for(sb = nl_sub_0(at); sb; sb = nl_sub_n(at, sb))
			if(!(fq = nl_u32(sb)))
				continue;
			else if(*fq == sc->freq)
				break;
		if(sb) sc->flags |= SF_STALE;
	}
}

static void drop_stale_scan_slots(void)
{
	struct scan* sc;

	for(sc = scans; sc < scans + nscans; sc++)
		if(sc->flags & SF_STALE)
			free_scan_slot(sc);
}

static int get_i32_or_zero(struct nlattr* bss, int key)
{
	int32_t* val = nl_sub_i32(bss, key);
	return val ? *val : 0;
}

static void parse_scan_result(struct nlgen* msg)
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

	memcpy(sc->bssid, bssid, 6);
	sc->freq = get_i32_or_zero(bss, NL80211_BSS_FREQUENCY);
	sc->signal = get_i32_or_zero(bss, NL80211_BSS_SIGNAL_MBM);
	sc->type = 0;
	sc->flags &= ~SF_STALE;

	if((ies = nl_sub(bss, NL80211_BSS_INFORMATION_ELEMENTS)))
		parse_station_ies(sc, ies->payload, nl_attr_len(ies));
}

static void msg_scan_res(struct link* ls, struct nlgen* msg)
{
	if(msg->nlm.flags & NLM_F_MULTI) {
		parse_scan_result(msg);
	} else if(ls->ifi == genl_scan_ifi) {
		mark_stale_scan_slots(msg);
		request_scan_results();
	}
}

struct cmdh {
	int cmd;
	void (*func)(struct link* ls, struct nlgen* msg);
} genlcmds[] = {
	{ NL80211_CMD_NEW_INTERFACE,    msg_new_wifi       },
	{ NL80211_CMD_DEL_INTERFACE,    msg_del_wifi       },
	{ NL80211_CMD_TRIGGER_SCAN,     msg_scan_start     },
	{ NL80211_CMD_SCAN_ABORTED,     msg_scan_abort     },
	{ NL80211_CMD_NEW_SCAN_RESULTS, msg_scan_res       },
	{ NL80211_CMD_AUTHENTICATE,     NULL               },
	{ NL80211_CMD_ASSOCIATE,        NULL               },
	{ NL80211_CMD_DEAUTHENTICATE,   NULL               },
	{ NL80211_CMD_DISASSOCIATE,     NULL               },
	{ NL80211_CMD_CONNECT,          NULL               },
	{ NL80211_CMD_DISCONNECT,       NULL               },
	{ NL80211_CMD_NEW_STATION,      NULL               },
	{ NL80211_CMD_DEL_STATION,      NULL               },
	{ NL80211_CMD_NOTIFY_CQM,       NULL               },
	{ 0, NULL }
};

static void handle_nl80211(struct nlgen* msg)
{
	struct cmdh* ch;
	struct link* ls;
	int ifi;

	if(!(ifi = get_genl_ifindex(msg)))
		return;
	if((ls = find_link_slot(ifi)))
		; /* good, we know this link already */
	else if(msg->cmd != NL80211_CMD_NEW_INTERFACE)
		return; /* hm strange */
	else
		return msg_new_wifi_early(ifi, msg);

	for(ch = genlcmds; ch->cmd; ch++)
		if(ch->cmd == msg->cmd)
			break;

	if(ch->func)
		ch->func(ls, msg);
	else if(ch->cmd)
		return;
	else
		nl_dump_genl(&msg->nlm);
}

static void handle_genl_done(struct nlmsg* nlm)
{
	if(genl_dump_state == DUMP_SCAN) {
		drop_stale_scan_slots();
		wifi_scan_done();
	}
	if(genl_scan_ready)
		request_scan_results();
	else
		genl_dump_state = DUMP_NONE;
}

static void handle_genl_error(struct nlmsg* nlm)
{
	struct nlerr* msg;

	if(!(msg = nl_err(nlm)))
		return;

	if(msg->seq == genl_scan_seq)
		wifi_scan_fail(msg->errno);
	else
		warn("genl", NULL, msg->errno);
}

void handle_genl(struct nlmsg* nlm)
{
	struct nlgen* msg;
	int type = nlm->type;

	if(type == NLMSG_NOOP)
		return;
	else if(type == NLMSG_ERROR)
		handle_genl_error(nlm);
	else if(type == NLMSG_DONE)
		handle_genl_done(nlm);
	else if(type != nl80211)
		return;

	if(!(msg = nl_gen(nlm)))
		return;

	handle_nl80211(msg);
}

static void subscribe(struct nlpair* g)
{
	int ret;

	if(g->id <= 0)
		fail("NL group nl80211", g->name, -ENOENT);
	if((ret = nl_subscribe(&genl, g->id)) < 0)
		fail("NL subscribe nl80211", g->name, ret);
}

void setup_genl(void)
{
	char* family = "nl80211";
	struct nlpair grps[] = {
		{ -1, "config" },
		{ -1, "mlme" },
		{ -1, "scan" },
		{  0, NULL } };

	nl_init(&genl);
	nl_set_txbuf(&genl, genl_tx, sizeof(genl_tx));
	nl_set_rxbuf(&genl, genl_rx, sizeof(genl_rx));
	nl_connect(&genl, NETLINK_GENERIC, 0);

	if((nl80211 = query_family_grps(&genl, family, grps)) < 0)
		fail("NL family", family, nl80211);

	subscribe(&grps[0]);
	subscribe(&grps[1]);
	subscribe(&grps[2]);

	request_wifi_list();
}
