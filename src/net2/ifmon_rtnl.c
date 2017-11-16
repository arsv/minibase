#include <bits/errno.h>
#include <bits/socket/inet.h>

#include <netlink.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <netlink/rtnl/mgrp.h>

#include <string.h>
#include <util.h>

#include "ifmon.h"

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

	quit("send", "rtnl", rtnl.err);
}

static void set_iface_state(int ifi, int mask, int bits)
{
	struct ifinfomsg* msg;

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

void set_iface_address(int ifi, uint8_t ip[4], int mask, int lt, int rt)
{
	struct ifaddrmsg* req;

	nl_header(&rtnl, req, RTM_NEWADDR, NLM_F_CREATE | NLM_F_REPLACE,
		.family = AF_INET,
		.prefixlen = mask,
		.flags = 0,
		.scope = 0,
		.index = ifi);
	nl_put(&rtnl, IFA_LOCAL, ip, 4);

	if(rt > lt) /* renew time must be less than lease time */
		rt = lt;
	if(lt) {
		struct ifa_cacheinfo ci = {
			.valid = lt,
			.prefered = rt,
			.created = 0,
			.updated = 0
		};
		nl_put(&rtnl, IFA_CACHEINFO, &ci, sizeof(ci));
	}

	rtnl_send_check();
}

void add_default_route(int ifi, uint8_t gw[4])
{
	struct rtmsg* req;
	uint32_t oif = ifi;

	nl_header(&rtnl, req, RTM_NEWROUTE, NLM_F_CREATE | NLM_F_REPLACE,
		.family = ARPHRD_EETHER,
		.dst_len = 0,
		.src_len = 0,
		.tos = 0,
		.table = RT_TABLE_MAIN,
		.protocol = RTPROT_DHCP,
		.scope = RT_SCOPE_UNIVERSE,
		.type = RTN_UNICAST,
		.flags = 0);
	nl_put(&rtnl, RTA_OIF, &oif, sizeof(oif));
	nl_put(&rtnl, RTA_GATEWAY, gw, 4);

	rtnl_send_check();
}

static int iff_to_flags(int iff, int flags)
{
	flags &= ~(LF_ENABLED | LF_CARRIER);
	
	if(iff & IFF_RUNNING)
		flags |= LF_CARRIER;
	if(iff & IFF_UP)
		flags |= LF_ENABLED;

	return flags;
}

/* Avoid tracking netdevs that lack MAC address.
   This should exclude ppp ifaces among other things. */

static int check_set_mac(struct link* ls, struct ifinfomsg* msg)
{
	struct nlattr* at;

	if(!(at = ifi_get(msg, IFLA_ADDRESS)))
		return 0;
	if(at->len - sizeof(*at) != 6)
		return 0;

	memcpy(ls->mac, at->payload, 6);

	return 1;
}

static int bitgain(int prev, int curr, int bit)
{
	return (!(prev & bit) && (curr & bit));
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

	int prev = ls->flags;
	int curr = iff_to_flags(msg->flags, ls->flags);

	if(!ls->ifi) {
		/* new link notification */

		if(!check_set_mac(ls, msg))
			return;

		ls->ifi = msg->index;
		ls->flags = curr;
		memcpy(ls->name, name, nlen);

		link_new(ls);

		if(ls->flags & LF_ENABLED)
			link_enabled(ls);
		if(ls->flags & LF_CARRIER)
			link_carrier(ls);

	} else if(curr != prev) {
		/* state change notification */

		ls->flags = curr;

		if(bitgain(curr, prev, LF_CARRIER))
			link_lost(ls);
		if(bitgain(curr, prev, LF_ENABLED))
			link_down(ls);
		if(bitgain(prev, curr, LF_ENABLED))
			link_enabled(ls);
		if(bitgain(prev, curr, LF_CARRIER))
			link_carrier(ls);
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

void delete_addr(LS)
{
	struct ifaddrmsg* req;

	nl_header(&rtnl, req, RTM_DELADDR, NLM_F_ACK,
		.family = AF_INET,
		.prefixlen = 0,
		.flags = 0,
		.scope = 0,
		.index = ls->ifi);

	rtnl_send_check();

	ls->seq = rtnl.seq;
}

static void seq_error(LS, int errno)
{
	if(ls->flags & LF_FLUSHING) {
		if(!errno)
			delete_addr(ls);
		else
			link_flushed(ls);
	} else if(errno) {
		warn("rtnl", ls->name, errno);
	}
}

static void msg_rtnl_err(struct nlerr* msg)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;
		if(ls->seq != msg->seq)
			continue;

		ls->seq = 0;
		seq_error(ls, msg->errno);

		return;
	}

	warn("rtnl", "error", msg->errno);
}

static void trigger_link_dump(void)
{
	struct ifinfomsg* req;

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
#undef MSG
	{ 0, NULL, 0 }
};

static void dispatch(struct nlmsg* msg)
{
	struct rtnh* rh;

	for(rh = rtnlcmds; rh->hdr; rh++)
		if(msg->type == rh->type)
			break;
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
