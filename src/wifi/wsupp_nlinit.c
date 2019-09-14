#include <netlink.h>
#include <netlink/attr.h>
#include <netlink/pack.h>
#include <netlink/recv.h>
#include <netlink/genl/ctrl.h>
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

struct nlpair {
	int id;
	char* name;
};

static int nle(const char* msg, const char* arg, int ret)
{
	warn(msg, arg, ret);
	return ret < 0 ? ret : -1;
}

static int send_recv(int fd, struct nlgen** out)
{
	int ret;
	struct nlmsg* msg;
	struct nlerr* err;
	struct nlgen* gen;

	*out = NULL;

	if((ret = nc_send(fd, &nc)) < 0)
		return ret;
	if((ret = nr_recv(fd, &nr)) < 0)
		return ret;

	if(!(msg = nr_next(&nr)))
		return -EAGAIN;

	if((err = nl_err(msg))) {
		if((ret = err->errno) < 0)
			return ret;
		else
			return -EBADMSG;
	} else if(!(gen = nl_gen(msg))) {
		return -EBADMSG;
	}

	*out = gen;

	return 0;
}

static int find_group(struct nlattr* groups, char* name)
{
	struct nlattr* at;

	for(at = nl_sub_0(groups); at; at = nl_sub_n(groups, at)) {
		if(!nl_attr_is_nest(at))
			continue;

		char* gn = nl_sub_str(at, 1);
		uint32_t* id = nl_sub_u32(at, 2);

		if(!gn || !id)
			continue;
		if(strcmp(gn, name))
			continue;

		return *id;
	}

	return -ENOENT;
}

static int query_family(int fd, char* name, struct nlpair* grps, int ngrps)
{
	struct nlgen* msg;
	int ret;

	nc_header(&nc, GENL_ID_CTRL, 0, 0);
	nc_gencmd(&nc, CTRL_CMD_GETFAMILY, 1);
	nc_put_str(&nc, CTRL_ATTR_FAMILY_NAME, name);

	if((ret = send_recv(fd, &msg)))
		return ret;

	uint16_t* grpid = nl_get_u16(msg, CTRL_ATTR_FAMILY_ID);
	struct nlattr* groups = nl_get_nest(msg, CTRL_ATTR_MCAST_GROUPS);

	if(!grpid)
		return -EBADMSG;
	if(!groups)
		return -ENOENT;

	for(int i = 0; i < ngrps; i++) {
		struct nlpair* np = &grps[i];
		int id;

		if((id = find_group(groups, np->name)) <= 0)
			return -ENOENT;

		np->id = id;
	}

	return *grpid;
}

static int query_iface(int fd, int ifi)
{
	struct nlgen* msg;
	struct nlattr* mac;
	char* name;
	int ret;

	nc_header(&nc, nl80211, 0, 0);
	nc_gencmd(&nc, NL80211_CMD_GET_INTERFACE, 0);
	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifi);

	if((ret = send_recv(fd, &msg)) < 0)
		return nle("NL80211_CMD_GET_INTERFACE", NULL, ret);
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

static int query_wiphy(int fd, int ifi)
{
	struct nlgen* msg;
	struct nlattr* cmds;
	struct nlattr* at;
	int ret, *cmd;
	int got_authenticate = 0;
	int got_connect = 0;

	nc_header(&nc, nl80211, 0, 0);
	nc_gencmd(&nc, NL80211_CMD_GET_WIPHY, 0);
	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifi);

	if((ret = send_recv(fd, &msg)))
		return nle("NL80211_CMD_GET_WIPHY", NULL, ret);
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

int init_netlink(int fd, int ifi)
{
	char* family = "nl80211";
	struct nlpair grps[] = {
		{ -1, "mlme" },
		{ -1, "scan" } };
	int ret;

	(void)ifi;

	if((ret = query_family(fd, family, grps, ARRAY_SIZE(grps))) < 0)
		return nle("NL family", family, ret);
	if(grps[0].id < 0)
		return nle("NL group nl80211", grps[0].name, -ENOENT);
	if(grps[1].id < 0)
		return nle("NL group nl80211", grps[1].name, -ENOENT);

	nl80211 = ret;

	if((ret = query_iface(fd, ifi)) < 0)
		return ret;
	if((ret = query_wiphy(fd, ifi)) < 0)
		return ret;

	if((ret = nl_subscribe(fd, grps[0].id)) < 0)
		return nle("NL subscribe nl80211", grps[0].name, ret);
	if((ret = nl_subscribe(fd, grps[1].id)) < 0)
		return nle("NL subscribe nl80211", grps[1].name, ret);

	return 0;
}
