#include <sys/setsockopt.h>

#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <format.h>
#include <fail.h>

#include "wimon.h"

struct netlink genl;

char genl_tx[512];
char genl_rx[4*4096];
int nl80211;

void trigger_scan(int ifi)
{
	nl_new_cmd(&genl, nl80211, NL80211_CMD_TRIGGER_SCAN, 0);
	nl_put_u64(&genl, NL80211_ATTR_IFINDEX, ifi);

	if(nl_send(&genl))
		fail("send", "genl", genl.err);
}

static void trigger_results(int ifi)
{
	nl_new_cmd(&genl, nl80211, NL80211_CMD_GET_SCAN, 0);
	nl_put_u64(&genl, NL80211_ATTR_IFINDEX, ifi);

	if(nl_send_dump(&genl))
		fail("send", "genl", genl.err);
}

static void trigger_wilist(void)
{
	nl_new_cmd(&genl, nl80211, NL80211_CMD_GET_INTERFACE, 0);

	if(nl_send_dump(&genl))
		fail("send", "genl", genl.err);
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

	eprintf("new-wifi %i\n", ls->ifi);

	trigger_scan(ls->ifi);
}

static void msg_del_wifi(struct link* ls, struct nlgen* msg)
{
	ls->flags &= ~F_WIFI;

	drop_scan_slots_for(ls->ifi);

	eprintf("del-wifi %i\n", ls->ifi);
}

static void msg_scan_start(struct link* ls, struct nlgen* msg)
{
	ls->seq++;
	ls->flags |= F_SCAN;

	eprintf("scan-start %s\n", ls->name);
}

static void msg_scan_abort(struct link* ls, struct nlgen* msg)
{
	ls->flags &= ~F_SCAN;
	eprintf("scan-abort %s\n", ls->name);
}

static void msg_scan_res(struct link* ls, struct nlgen* msg)
{
	if(msg->nlm.flags & NLM_F_MULTI) {
		parse_scan_result(ls, msg);
	} else {
		ls->flags &= ~F_SCAN;
		drop_stale_scan_slots(ls->ifi, ls->seq);
		trigger_results(ls->ifi);
	}
}

static void msg_connect(struct link* ls, struct nlgen* msg)
{
	uint8_t* bssid;

	if((bssid = nl_get_of_len(msg, NL80211_ATTR_MAC, 6))) {
		eprintf("connect %02X:%02X:%02X:%02X:%02X:%02X\n",
				bssid[0], bssid[1], bssid[2],
				bssid[3], bssid[4], bssid[5]);
		memcpy(ls->bssid, bssid, 6);
	} else {
		eprintf("connect ???\n");
		memset(ls->bssid, 0, 6);
	}

	ls->flags |= F_CONN;
}

static void msg_disconnect(struct link* ls, struct nlgen* msg)
{
	ls->flags &= ~F_CONN;
	memset(ls->bssid, 0, 6);
	eprintf("disconnect\n");
}

static void msg_associate(struct link* ls, struct nlgen* msg)
{
	eprintf("associate %i\n", ls->ifi);
	ls->flags |= F_ASSOC;
}

static void msg_authenticate(struct link* ls, struct nlgen* msg)
{
	eprintf("authenticate %i\n", ls->ifi);
	ls->flags |= F_AUTH;
}

static void msg_deauthenticate(struct link* ls, struct nlgen* msg)
{
	eprintf("deauthenticate %i\n", ls->ifi);
	ls->flags &= ~F_AUTH;
}

static void msg_disassociate(struct link* ls, struct nlgen* msg)
{
	eprintf("disassociate %i\n", ls->ifi);
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
	else if(type != nl80211)
		return;

	if(!(msg = nl_gen(nlm)))
		return;

	handle_nl80211(msg);
}

/* Setup part */

/* CTRL_CMD_GETFAMILY provides both family id *and* multicast group ids
   we need for subscription. So we do it all in a single request. */

struct nlpair {
	const char* name;
	int id;
};

static void query_nl_family(struct netlink* nl,
                            struct nlpair* fam, struct nlpair* mcast)
{
	struct nlgen* gen;
	struct nlattr* at;

	nl_new_cmd(nl, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 1);
	nl_put_str(nl, CTRL_ATTR_FAMILY_NAME, fam->name);

	if(!(gen = nl_send_recv_genl(nl)))
		fail("CTRL_CMD_GETFAMILY", fam->name, nl->err);

	uint16_t* grpid = nl_get_u16(gen, CTRL_ATTR_FAMILY_ID);
	struct nlattr* groups = nl_get_nest(gen, CTRL_ATTR_MCAST_GROUPS);

	if(!grpid)
		fail("unknown nl family", fam->name, 0);
	if(!groups)
		fail("no mcast groups for", fam->name, 0);

	fam->id = *grpid;

	for(at = nl_sub_0(groups); at; at = nl_sub_n(groups, at)) {
		if(!nl_attr_is_nest(at))
			continue;

		char* name = nl_sub_str(at, 1);
		uint32_t* id = nl_sub_u32(at, 2);

		if(!name || !id)
			continue;

		struct nlpair* mc;
		for(mc = mcast; mc->name; mc++)
			if(!strcmp(name, mc->name))
				mc->id = *id;
	}
}

static void socket_subscribe(struct netlink* nl, int id, const char* name)
{
	int fd = nl->fd;
	int lvl = SOL_NETLINK;
	int opt = NETLINK_ADD_MEMBERSHIP;

	xchk(syssetsockopt(fd, lvl, opt, &id, sizeof(id)),
		"setsockopt NETLINK_ADD_MEMBERSHIP", name);
}

static int resolve_80211_subscribe_scan(struct netlink* nl)
{
	struct nlpair fam = { "nl80211", -1 };
	struct nlpair mcast[] = {
		{ "config", -1 },  /* wifi exts on a link */
		{ "mlme", -1 },    /* assoc/auth/connect */
		{ "scan", -1 },    /* start/stop/results */
		{ NULL, 0 } };

	query_nl_family(nl, &fam, mcast);

	struct nlpair* p;

	for(p = mcast; p->name; p++)
		if(p->id >= 0)
			socket_subscribe(nl, p->id, p->name);
		else
			fail("unknown 802.11 mcast group", p->name, 0);

	return fam.id;
}

void setup_genl(void)
{
	nl_init(&genl);
	nl_set_txbuf(&genl, genl_tx, sizeof(genl_tx));
	nl_set_rxbuf(&genl, genl_rx, sizeof(genl_rx));
	nl_connect(&genl, NETLINK_GENERIC, 0);

	nl80211 = resolve_80211_subscribe_scan(&genl);

	trigger_wilist();
}
