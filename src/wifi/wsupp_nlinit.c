#include <netlink.h>
#include <netlink/genl.h>
#include <netlink/genl/nl80211.h>

#include <string.h>
#include <util.h>

#include "wsupp.h"
#include "wsupp_netlink.h"

int ifindex;
char ifname[32];
byte ifaddr[6];
int nl80211; /* family id */
int drvconnect;

static int nle(const char* msg, const char* arg, int ret)
{
	warn(msg, arg, ret);
	return ret < 0 ? ret : -1;
}

static int query_iface(int ifi)
{
	struct nlgen* msg;
	struct nlattr* mac;
	char* name;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_GET_INTERFACE, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifi);

	if(!(msg = nl_send_recv_genl(&nl)))
		return nle("NL80211_CMD_GET_INTERFACE", NULL, nl.err);

	if(!(name = nl_get_str(msg, NL80211_ATTR_IFNAME)))
		return nle("NL80211_ATTR_IFNAME", NULL, -ENOENT);
	if(strlen(name) > sizeof(ifname)-1)
		return nle("NL80211_ATTR_IFNAME", name, -E2BIG);
	if(!(mac = nl_get(msg, NL80211_ATTR_MAC)))
		return nle("NL80211_ATTR_MAC", NULL, -ENOENT);
	if(nl_paylen(mac) != 6)
		return nle("NL80211_ATTR_MAC", "length", nl_paylen(mac));

	ifindex = ifi;
	memcpy(ifname, name, strlen(name)+1);
	memcpy(ifaddr, mac->payload, 6);

	return 0;
}

static int query_wiphy(int ifi)
{
	struct nlgen* msg;
	struct nlattr* cmds;
	struct nlattr* at;
	int* cmd;
	int got_authenticate = 0;
	int got_connect = 0;

	nl_new_cmd(&nl, nl80211, NL80211_CMD_GET_WIPHY, 0);
	nl_put_u32(&nl, NL80211_ATTR_IFINDEX, ifi);

	if(!(msg = nl_send_recv_genl(&nl)))
		return nle("NL80211_CMD_GET_WIPHY", NULL, nl.err);
	if(!(cmds = nl_get(msg, NL80211_ATTR_SUPPORTED_COMMANDS)))
		return nle("NL80211_ATTR_SUPPORTED_COMMANDS", NULL, -ENOENT);
	if(!(cmds = nl_nest(cmds)))
		return nle("NL80211_ATTR_SUPPORTED_COMMANDS", NULL, -EINVAL);

	for(at = nl_sub_0(cmds); at; at = nl_sub_n(cmds, at))
		if(!(cmd = nl_i32(at)))
			continue;
		else if(*cmd == NL80211_CMD_AUTHENTICATE)
			got_authenticate = 1;
		else if(*cmd == NL80211_CMD_CONNECT)
			got_connect = 1;

	if(!got_connect)
		return nle("NL80211_CMD_CONNECT", "not supported", 0);

	drvconnect = !got_authenticate;

	return 0;
}

int init_netlink(int ifi)
{
	char* family = "nl80211";
	struct nlpair grps[] = {
		{ -1, "mlme" },
		{ -1, "scan" },
		{  0, NULL } };
	int ret;

	(void)ifi;

	if((ret = query_family_grps(&nl, family, grps)) < 0)
		return nle("NL family", family, ret);
	if(grps[0].id < 0)
		return nle("NL group nl80211", grps[0].name, -ENOENT);
	if(grps[1].id < 0)
		return nle("NL group nl80211", grps[1].name, -ENOENT);

	nl80211 = ret;

	if((ret = query_iface(ifi)) < 0)
		return ret;
	if((ret = query_wiphy(ifi)) < 0)
		return ret;

	if((ret = nl_subscribe(&nl, grps[0].id)) < 0)
		return nle("NL subscribe nl80211", grps[0].name, ret);
	if((ret = nl_subscribe(&nl, grps[1].id)) < 0)
		return nle("NL subscribe nl80211", grps[1].name, ret);

	return 0;
}
