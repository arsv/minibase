#include <bits/socket/inet.h>
#include <bits/errno.h>

#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/genl/nl80211.h>
#include <netlink/dump.h>

#include <string.h>
#include <output.h>
#include <format.h>
#include <heap.h>
#include <util.h>
#include <main.h>

/* Dumping ip stack state has almost nothing in common with configuring it,
   so this is not a part of ip4cfg, at least for now.

   Unlike ip4cfg which is expected to be used in startup scripts and such,
   this tool is only expected to be run manually, by a human user trying
   to figure out what's wrong with the configuration. So it should provide
   a nice overview of the configuration, even if it requires combining data
   from multiple RTNL requests. */

ERRTAG("getif");

struct netlink nl;
char txbuf[512];
char rxbuf[7*1024];

/* Link and IP lists are cached in the heap, because we need freely
   accessible to format links and routes properly. Route data is not
   cached, just show one message at a time. */

int main(int argc, char** argv)
{
	int ifi;
	int ret;
	char* p;
	char* family = "nl80211";
	struct nlpair grps[] = {
		{ -1, "mlme" },
		{ -1, "scan" },
		{  0, NULL } };
	struct nlgen* msg;

	if(argc != 2)
		fail("bad call", NULL, 0);

	if(!(p = parseint(argv[1], &ifi)) || *p)
		fail("bad ifindex:", argv[1], 0);

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));

	if((ret = nl_connect(&nl, NETLINK_GENERIC, 0)) < 0)
		fail("nl-connect", NULL, ret);

	if((ret = query_family_grps(&nl, family, grps)) < 0)
		fail("NL family", family, ret);
	if(grps[0].id < 0)
		fail("NL group nl80211", grps[0].name, -ENOENT);
	if(grps[1].id < 0)
		fail("NL group nl80211", grps[1].name, -ENOENT);

	int nl80211 = ret;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_GET_WIPHY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifi);

	if(!(msg = nl_send_recv_genl(&nl)))
		fail("netlink", NULL, nl.err);

	nl_dump_gen(msg);

	return 0;
}
