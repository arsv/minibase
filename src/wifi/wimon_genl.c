#include <sys/setsockopt.h>

#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <format.h>
#include <fail.h>

#include "wimon.h"
#include "nlfam.h"

/* NETLINK_GENERIC connection is used to request and fetch scan results,
   and also to track 802.11 stack state.

   RTNL and GENL use the same ifindexes to refer to the same devices.
   For wireless devices RTNL provides common state like IPs and routes
   while GENL provides wireless status updates. The code must be ready
   to see GENL packets before the same device appears in RTNL though,
   it is unlikely but may happen.

   In nl80211, there's a distinction between "phy" devices representing
   radios, and netdevs bound to phy-s. This part is not tracked here,
   at least for now. Wimon only deals with netdevs. Something else must
   be used to configure phy-net relations. Implementing ad-hoc or mesh
   networks may or may not require changes in this part. Likely anything
   that can be done with "iw dev" will be ok but "iw phy" will require
   phy tracking. */

struct netlink genl;

char genl_tx[512];
char genl_rx[4*4096];
int nl80211;

/* Can't request two dumps at the same time. See RTNL on this.
   A bit different handling here, there's one possible dump per link
   and also wifi list dump which only ever gets run once. */

int genl_scan_ready;
int genl_dump_lock;

void trigger_scan(int ifi)
{
	nl_new_cmd(&genl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u64(&genl, NL80211_ATTR_IFINDEX, ifi);

	if(nl_send(&genl))
		fail("send", "genl", genl.err);
}

static void request_results(int ifi)
{
	nl_new_cmd(&genl, nl80211, NL80211_CMD_GET_SCAN, 0);
	nl_put_u64(&genl, NL80211_ATTR_IFINDEX, ifi);

	if(nl_send_dump(&genl))
		fail("send", "genl", genl.err);

	genl_dump_lock = 1;
}

static void request_wifi_list(void)
{
	nl_new_cmd(&genl, nl80211, NL80211_CMD_GET_INTERFACE, 0);

	if(nl_send_dump(&genl))
		fail("send", "genl", genl.err);

	genl_dump_lock = 1;
}

static int get_genl_ifindex(struct nlgen* msg)
{
	uint32_t* ifi = nl_get_u32(msg, NL80211_ATTR_IFINDEX);
	return ifi ? *ifi : 0;
}

static struct link* find_genl_link(struct nlgen* msg)
{
	int ifi = get_genl_ifindex(msg);
	return ifi ? find_link_slot(ifi) : NULL;
}

static struct link* grab_genl_link(struct nlgen* msg)
{
	int ifi = get_genl_ifindex(msg);
	struct link* ls = ifi ? grab_link_slot(ifi) : NULL;
	if(ls && !ls->ifi) ls->ifi = ifi;
	return ls;
}

static void msg_new_wifi(struct link* ls, struct nlgen* msg)
{
	if(ls->flags & F_WIFI)
		return;

	ls->flags |= F_WIFI;

	eprintf("new-wifi %s\n", ls->name);

	trigger_scan(ls->ifi);
}

static void msg_del_wifi(struct link* ls, struct nlgen* msg)
{
	ls->flags &= ~F_WIFI;

	drop_scan_slots_for(ls->ifi);

	eprintf("del-wifi %s\n", ls->name);
}

static void msg_scan_start(struct link* ls, struct nlgen* msg)
{
	ls->seq++;
	ls->flags |= F_SCANNING;

	eprintf("scan-start %s\n", ls->name);
}

static void msg_scan_abort(struct link* ls, struct nlgen* msg)
{
	ls->flags &= ~F_SCANNING;
	eprintf("scan-abort %s\n", ls->name);
}

static void msg_scan_res(struct link* ls, struct nlgen* msg)
{
	if(msg->nlm.flags & NLM_F_MULTI) {
		parse_scan_result(ls, msg);
	} else {
		ls->flags &= ~F_SCANNING;
		genl_scan_ready = 1;
		drop_stale_scan_slots(ls->ifi, ls->seq);

		if(genl_dump_lock) {
			ls->flags |= F_SCANRES;
			genl_scan_ready = 1;
		} else {
			request_results(ls->ifi);
		}
	}
}

static void msg_connect(struct link* ls, struct nlgen* msg)
{
	uint8_t* bssid;

	if((bssid = nl_get_of_len(msg, NL80211_ATTR_MAC, 6))) {
		eprintf("connect %s to %02X:%02X:%02X:%02X:%02X:%02X\n",
				ls->name,
				bssid[0], bssid[1], bssid[2],
				bssid[3], bssid[4], bssid[5]);
		memcpy(ls->bssid, bssid, 6);
	} else {
		eprintf("connect %s to ???\n", ls->name);
		memset(ls->bssid, 0, 6);
	}

	ls->flags |= F_CONNECT;
}

static void msg_disconnect(struct link* ls, struct nlgen* msg)
{
	ls->flags &= ~F_CONNECT;
	memset(ls->bssid, 0, 6);
	eprintf("wifi %s disconnected\n", ls->name);
}

static void msg_associate(struct link* ls, struct nlgen* msg)
{
	eprintf("wifi %s associated\n", ls->name);
	ls->flags |= F_ASSOC;
}

static void msg_authenticate(struct link* ls, struct nlgen* msg)
{
	eprintf("wifi %s authenticated\n", ls->name);
	ls->flags |= F_AUTH;
}

static void msg_deauthenticate(struct link* ls, struct nlgen* msg)
{
	eprintf("wifi %s deauthenticated\n", ls->name);
	ls->flags &= ~F_AUTH;
}

static void msg_disassociate(struct link* ls, struct nlgen* msg)
{
	eprintf("wifi %s disassociated\n", ls->name);
	ls->flags &= ~F_ASSOC;
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
	{ NL80211_CMD_ASSOCIATE,        msg_associate      },
	{ NL80211_CMD_AUTHENTICATE,     msg_authenticate   },
	{ NL80211_CMD_DEAUTHENTICATE,   msg_deauthenticate },
	{ NL80211_CMD_DISASSOCIATE,     msg_disassociate   },
	{ NL80211_CMD_CONNECT,          msg_connect        },
	{ NL80211_CMD_DISCONNECT,       msg_disconnect     },
	{ NL80211_CMD_NEW_STATION,      NULL               },
	{ NL80211_CMD_DEL_STATION,      NULL               },
	{ 0, NULL }
};

static void handle_nl80211(struct nlgen* msg)
{
	struct link* ls;
	struct cmdh* ch;

	if(msg->cmd == NL80211_CMD_NEW_INTERFACE)
		ls = grab_genl_link(msg);
	else
		ls = find_genl_link(msg);
	if(!ls) return;

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
	int i;

	genl_dump_lock = 0;

	if(!genl_scan_ready)
		return;

	for(i = 0; i < nlinks; i++)
		if(links[i].flags & F_SCANRES)
			break;

	if(i < nlinks) {
		links[i].flags &= ~F_SCANRES;
		request_results(links[i].ifi);
	} else {
		genl_scan_ready = 0;
	}
}

static void handle_genl_error(struct nlmsg* nlm)
{
	struct nlerr* msg;

	if(!(msg = nl_err(nlm)))
		return;

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
