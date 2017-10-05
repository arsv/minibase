#include <sys/file.h>
#include <sys/mman.h>

#include <netlink.h>
#include <netlink/genl/ctrl.h>
#include <netlink/dump.h>

#include <errtag.h>
#include <string.h>
#include <util.h>

ERRTAG("gnevents");

char TX[1*1024];
char RX[15*1024];

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

static int resolve_80211_subscribe_scan(struct netlink* nl)
{
	struct nlpair fam = { "nl80211", -1 };
	struct nlpair mcast[] = {
		{ "config", -1 },
		{ "mlme", -1 },
		{ "scan", -1 },
		{ NULL, 0 } };

	query_nl_family(nl, &fam, mcast);

	struct nlpair* p;

	for(p = mcast; p->name; p++)
		if(p->id >= 0)
			xchk(nl_subscribe(nl, p->id), "nl-subscribe", p->name);
		else
			fail("unknown 802.11 mcast group", p->name, 0);

	return fam.id;
}

int main(void)
{
	struct netlink nl;
	struct nlmsg* msg;

	nl_init(&nl);
	nl_set_txbuf(&nl, TX, sizeof(TX));
	nl_set_rxbuf(&nl, RX, sizeof(RX));
	
	xchk(nl_connect(&nl, NETLINK_GENERIC, 0),
		"connect", "NETLINK_ROUTE");

	resolve_80211_subscribe_scan(&nl);

	while((msg = nl_recv(&nl))) {
		nl_dump_genl(msg);
	} if(nl.err) {
		fail("nl-recv", NULL, nl.err);
	}

	return 0;
}
