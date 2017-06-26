#include <bits/errno.h>
#include <bits/socket/inet.h>

#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <netlink/rtnl/mgrp.h>

#include <string.h>
#include <fail.h>

#include "wimon.h"

/* NETLINK_ROUTE connection is used to keep up-to-date list
   of available net devices and their (generic) state, like
   which of them have any ips assigned, which have default
   routes and so on. */

#define DUMP_LINKS  (1<<0)
#define DUMP_ADDRS  (1<<1)
#define DUMP_ROUTES (1<<2)

struct netlink rtnl;
int rtnl_dump_req;

char rtnl_tx[512];
char rtnl_rx[4096];

int rtnl_dump_pending;
int rtnl_dump_lock;

struct ifinfomsg* nl_ifi(struct nlmsg* nlm)
{
	if(nlm->len < sizeof(struct ifinfomsg))
		return NULL;
	return (struct ifinfomsg*) nlm;
}

struct nlattr* ifi_get(struct ifinfomsg* msg, int key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

struct nlattr* ifa_get(struct ifaddrmsg* msg, uint16_t key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static struct nlattr* rtm_get(struct rtmsg* msg, uint16_t key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static void rtnl_send(void)
{
	if(nl_send(&rtnl))
		fail("send", "rtnl", rtnl.err);
}

static void set_iface_state(int ifi, int mask, int bits)
{
	struct ifinfomsg* msg;

	nl_header(&rtnl, msg, RTM_SETLINK, 0,
			.index = ifi,
			.flags = bits,
			.change = mask);

	rtnl_send();
}

void enable_iface(int ifi)
{
	set_iface_state(ifi, IFF_UP, IFF_UP);
}

void disable_iface(int ifi)
{
	set_iface_state(ifi, IFF_UP, 0);
}

void del_link_addresses(int ifi)
{
	struct ifaddrmsg* req;

	nl_header(&rtnl, req, RTM_DELADDR, 0,
		.family = AF_INET,
		.prefixlen = 0,
		.flags = 0,
		.scope = 0,
		.index = ifi);

	rtnl_send();
}

static int iff_to_flags(int flags, int iff)
{
	flags &= ~(S_ENABLED | S_CARRIER);

	if(iff & IFF_UP)
		flags |= S_ENABLED;
	if(iff & IFF_RUNNING)
		flags |= S_CARRIER;

	return flags;
}

static int bitgain(int prev, int curr, int bit)
{
	return (!(prev & bit) && (curr & bit));
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

void msg_new_link(struct ifinfomsg* msg)
{
	struct link* ls;
	char* name;
	int nlen;

	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return;
	if((nlen = strlen(name)) > sizeof(ls->name)-1)
		return;
	if(msg->flags & (IFF_LOOPBACK | IFF_POINTOPOINT))
		return;
	if(!(ls = grab_link_slot(msg->index)))
		return;

	int prev = ls->flags;
	int curr = iff_to_flags(ls->flags, msg->flags);

	if(!ls->ifi) {
		/* new link notification */
		if(!looks_like_netcard(msg))
			return;
		ls->ifi = msg->index;
		ls->flags = curr;
		memcpy(ls->name, name, nlen);
		link_new(ls);
	} else if(curr != prev) {
		/* state change notification */
		ls->flags = curr;

		if(bitgain(curr, prev, S_CARRIER))
			link_down(ls);
		else if(bitgain(prev, curr, S_CARRIER))
			link_carrier(ls);
		else if(bitgain(prev, curr, S_ENABLED))
			link_enabled(ls);
		else if(bitgain(curr, prev, S_ENABLED))
			link_down(ls);
	}
}

void msg_del_link(struct ifinfomsg* msg)
{
	struct link* ls;

	if(!(ls = find_link_slot(msg->index)))
		return;

	link_gone(ls);

	free_link_slot(ls);
}

void msg_rtnl_err(struct nlerr* msg)
{
	warn("rtnl", NULL, msg->errno);
}

void msg_new_addr(struct ifaddrmsg* msg)
{
	struct link* ls;
	uint8_t* ip;

	if(!(ls = find_link_slot(msg->index)))
		return;
	if(!(ip = nl_bin(ifa_get(msg, IFA_ADDRESS), 4)))
		return;
	if(ls->flags & S_IPADDR)
		return;

	add_addr(ls->ifi, ADDR_IFACE, ip, msg->prefixlen);

	if(ls->flags & S_IPADDR)
		return;

	ls->flags |= S_IPADDR;
	link_ipaddr(ls);
}

void msg_del_addr(struct ifaddrmsg* msg)
{
	struct link* ls;
	uint8_t* ip;

	if(!(ls = find_link_slot(msg->index)))
		return;
	if(!(ip = nl_bin(ifa_get(msg, IFA_ADDRESS), 4)))
		return;
	if(!(ls->flags & S_IPADDR))
		return;

	del_addr(ls->ifi, ADDR_IFACE, ip, msg->prefixlen);

	if(get_addr(ls->ifi, ADDR_IFACE, NULL))
		return;

	ls->flags &= ~S_IPADDR;
	link_ipgone(ls);
}

/* Low-key default route tracking */

void msg_new_route(struct rtmsg* msg)
{
	uint32_t* oif;
	uint8_t *gw, none[4] = { 0, 0, 0, 0 };
	struct link* ls;

	if(msg->type != RTN_UNICAST)
		return;
	if(msg->dst_len)
		return;
	if(!(oif = nl_u32(rtm_get(msg, RTA_OIF))))
		return;
	if(!(ls = find_link_slot(*oif)))
		return;

	if(!(gw = nl_bin(rtm_get(msg, RTA_GATEWAY), 4)))
		gw = none;

	add_addr(ls->ifi, ADDR_UPLINK, gw, 0);
}

void msg_del_route(struct rtmsg* msg)
{
	uint32_t* oif;
	uint8_t *gw, none[4] = { 0, 0, 0, 0 };
	struct link* ls;

	if(msg->dst_len)
		return;
	if(!(oif = nl_u32(rtm_get(msg, RTA_OIF))))
		return;
	if(!(ls = find_link_slot(*oif)))
		return;

	if(!(gw = nl_bin(rtm_get(msg, RTA_GATEWAY), 4)))
		gw = none;

	del_addr(ls->ifi, ADDR_UPLINK, gw, 0);
}

/* At most one dump may be running at a time; requesting more results
   in EBUSY. Initially we need to run *three* of them, later del_addr
   and del_route may request their respective scans concurrently.
   To avoid errors, requests are serialized.

   NLMSG_DONE packets should never arrive unrequested on RTNL. */

static void send_dump_req(int cmd, int hdrsize)
{
	struct nlmsg* nlm;

	if(hdrsize < sizeof(struct nlmsg))
		fail("internal scan req inconsistent", NULL, 0);

	if((nlm = nl_start_packet(&rtnl, hdrsize)))
		*nlm = (struct nlmsg) {
			.len = hdrsize,
			.type = cmd,
			.flags = NLM_F_REQUEST | NLM_F_DUMP,
			.seq = rtnl.seq,
			.pid = 0
		};

	rtnl_send();
}

static void proceed_with_dump(void)
{
	int bit;
	int req = rtnl_dump_pending;

	if(!req)
		return;
	else if(req & (bit = DUMP_LINKS))
		send_dump_req(RTM_GETLINK, sizeof(struct ifinfomsg));
	else if(req & (bit = DUMP_ADDRS))
		send_dump_req(RTM_GETADDR, sizeof(struct ifaddrmsg));
	else if(req & (bit = DUMP_ROUTES))
		send_dump_req(RTM_GETROUTE, sizeof(struct rtmsg));
	else bit = 0;

	if(bit) {
		rtnl_dump_pending &= ~bit;
		rtnl_dump_lock = 1;
	} else {
		/* should not happen; stray bits in rtnl_dump_pending */
		rtnl_dump_pending = 0;
	}
}

static void msg_rtnl_done(struct nlmsg* msg)
{
	rtnl_dump_lock = 0;

	if(rtnl_dump_pending)
		proceed_with_dump();
}

static void request_rtnl_dump(int what)
{
	rtnl_dump_pending |= what;

	if(!rtnl_dump_lock)
		proceed_with_dump();
}

typedef void (*rth)(struct nlmsg* msg);

struct rtnh {
	int type;
	rth func;
	int hdr;
} rtnlcmds[] = {
#define MSG(cmd, func, mm) { cmd, (rth)(func), sizeof(struct mm) }
	MSG(NLMSG_NOOP,   NULL,          nlmsg),
	MSG(NLMSG_DONE,   msg_rtnl_done, nlmsg),
	MSG(NLMSG_ERROR,  msg_rtnl_err,  nlerr),
	MSG(RTM_NEWLINK,  msg_new_link,  ifinfomsg),
	MSG(RTM_DELLINK,  msg_del_link,  ifinfomsg),
	MSG(RTM_NEWADDR,  msg_new_addr,  ifaddrmsg),
	MSG(RTM_DELADDR,  msg_del_addr,  ifaddrmsg),
	MSG(RTM_NEWROUTE, msg_new_route, rtmsg),
	MSG(RTM_DELROUTE, msg_del_route, rtmsg),
#undef MSG
	{ 0, NULL, 0 }
};

void handle_rtnl(struct nlmsg* msg)
{
	struct rtnh* rh;

	for(rh = rtnlcmds; rh->hdr; rh++)
		if(msg->type == rh->type)
			break;
	if(!rh->hdr)
		nl_dump_rtnl(msg);
	if(!rh->hdr)
		return;
	if(msg->len < rh->hdr)
		return;
	if(!rh->func)
		return;

	rh->func(msg);
}

void setup_rtnl(void)
{
	int mgrp_link = RTMGRP_LINK | RTMGRP_NOTIFY;
	int mgrp_ipv4 = RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;

	nl_init(&rtnl);
	nl_set_txbuf(&rtnl, rtnl_tx, sizeof(rtnl_tx));
	nl_set_rxbuf(&rtnl, rtnl_rx, sizeof(rtnl_rx));
	nl_connect(&rtnl, NETLINK_ROUTE, mgrp_link | mgrp_ipv4);

	request_rtnl_dump(DUMP_LINKS | DUMP_ADDRS | DUMP_ROUTES);
}
