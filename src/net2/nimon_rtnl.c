#include <bits/errno.h>
#include <bits/socket/inet.h>

#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <netlink/rtnl/mgrp.h>

#include <printf.h>
#include <string.h>
#include <util.h>

#include "nimon.h"

struct netlink rtnl;
int rtnl_dump_req;
int netlink;

char rtnl_tx[512];
char rtnl_rx[4096];

static struct nlattr* ifi_get(struct ifinfomsg* msg, int key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static void rtnl_send_check(void)
{
	if(nl_send(&rtnl) >= 0)
		return;

	fail("send", "rtnl", rtnl.err);
}

static void set_iface_state(int ifi, int mask, int bits)
{
	struct ifinfomsg* msg;

	tracef("%s ifi=%i bits=%i\n", __FUNCTION__, ifi, bits);

	nl_header(&rtnl, msg, RTM_SETLINK, 0,
			.index = ifi,
			.flags = bits,
			.change = mask);

	rtnl_send_check();
}

void enable_iface(LS)
{
	set_iface_state(ls->ifi, IFF_UP, IFF_UP);
}

void disable_iface(LS)
{
	set_iface_state(ls->ifi, IFF_UP, 0);
}

static int iff_to_wire(int iff)
{
	if(iff & IFF_RUNNING)
		return LW_CARRIER;
	if(iff & IFF_UP)
		return LW_ENABLED;

	return LW_DISABLED;
}

/* Avoid tracking netdevs that lack MAC address.
   This should exclude ppp ifaces among other things. */

static int looks_like_netcard(struct ifinfomsg* msg)
{
	struct nlattr* at;

	if(!(at = ifi_get(msg, IFLA_ADDRESS)))
		return 0;
	if(at->len - sizeof(*at) != 6)
		return 0;

	return 1;
}

static void msg_new_link(struct ifinfomsg* msg)
{
	struct link* ls;
	char* name;
	uint nlen;

	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return;
	if((nlen = strlen(name)) > sizeof(ls->name)-1)
		return;
	if(msg->flags & (IFF_LOOPBACK | IFF_POINTOPOINT))
		return;
	if(!(ls = grab_link_slot(msg->index)))
		return;

	int prev = ls->wire;
	int curr = iff_to_wire(msg->flags);

	if(!ls->ifi) {
		/* new link notification */
		if(!looks_like_netcard(msg))
			return;
		ls->ifi = msg->index;
		ls->wire = curr;
		memcpy(ls->name, name, nlen);
		link_new(ls);
	} else if(curr != prev) {
		/* state change notification */
		ls->wire = curr;

		if(curr == LW_DISABLED)
			link_down(ls);
		else if(prev == LW_DISABLED)
			link_enabled(ls);
		else if(curr == LW_CARRIER)
			link_carrier(ls);
		else if(prev == LW_CARRIER)
			link_lost(ls);
	}
}

static void msg_del_link(struct ifinfomsg* msg)
{
	struct link* ls;

	if(!(ls = find_link_slot(msg->index)))
		return;

	link_gone(ls);

	free_link_slot(ls);
}

//static void msg_new_addr(struct ifaddrmsg* msg)
//{
//
//}
//
//static void msg_del_addr(struct ifaddrmsg* msg)
//{
//
//}

static void msg_rtnl_err(struct nlerr* msg)
{
	warn("rtnl", "error", msg->errno);
}

static void trigger_link_dump(void)
{
	struct ifinfomsg* req;

	tracef("%s\n", __FUNCTION__);

	nl_header(&rtnl, req, RTM_GETLINK, NLM_F_REQUEST | NLM_F_DUMP);

	rtnl_send_check();
}

static void msg_rtnl_done(struct nlmsg* msg)
{
	(void)msg;
}

typedef void (*rth)(struct nlmsg* msg);

struct rtnh {
	int type;
	rth func;
	uint hdr;
} rtnlcmds[] = {
#define MSG(cmd, func, mm) { cmd, (rth)(func), sizeof(struct mm) }
	MSG(NLMSG_NOOP,   NULL,          nlmsg),
	MSG(NLMSG_DONE,   msg_rtnl_done, nlmsg),
	MSG(NLMSG_ERROR,  msg_rtnl_err,  nlerr),
	MSG(RTM_NEWLINK,  msg_new_link,  ifinfomsg),
	MSG(RTM_DELLINK,  msg_del_link,  ifinfomsg),
//	MSG(RTM_NEWADDR,  msg_new_addr,  ifaddrmsg),
//	MSG(RTM_DELADDR,  msg_del_addr,  ifaddrmsg),
#undef MSG
	{ 0, NULL, 0 }
};

static void dispatch(struct nlmsg* msg)
{
	struct rtnh* rh;

	//nl_dump_rtnl(msg);

	for(rh = rtnlcmds; rh->hdr; rh++)
		if(msg->type == rh->type)
			break;
	//if(!rh->hdr)
	//	nl_dump_rtnl(msg);
	if(!rh->hdr)
		return;
	if(msg->len < rh->hdr)
		return;
	if(!rh->func)
		return;

	rh->func(msg);
}

void handle_rtnl(void)
{
	int ret;
	struct nlmsg* msg;

	if((ret = nl_recv_nowait(&rtnl)) < 0) {
		warn("recv", "rtnl", ret);
		return;
	}
	while((msg = nl_get_nowait(&rtnl)))
		dispatch(msg);

	nl_shift_rxbuf(&rtnl);
}

void setup_rtnl(void)
{
	int mgrp_link = RTMGRP_LINK | RTMGRP_NOTIFY;
	int mgrp_ipv4 = RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;

	nl_init(&rtnl);
	nl_set_txbuf(&rtnl, rtnl_tx, sizeof(rtnl_tx));
	nl_set_rxbuf(&rtnl, rtnl_rx, sizeof(rtnl_rx));
	nl_connect(&rtnl, NETLINK_ROUTE, mgrp_link | mgrp_ipv4);

	trigger_link_dump();

	netlink = rtnl.fd;
}
