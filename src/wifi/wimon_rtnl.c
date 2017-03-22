#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <netlink/rtnl/mgrp.h>

#include <format.h>
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

static void rtnl_send_check(void)
{
	if(nl_send(&rtnl))
		fail("send", "rtnl", rtnl.err);
}

static void request_link_dump(void)
{
	struct ifinfomsg* msg;
	nl_header(&rtnl, msg, RTM_GETLINK, NLM_F_DUMP);
	rtnl_send_check();
}

static void request_addr_dump(void)
{
	struct ifaddrmsg* msg;
	nl_header(&rtnl, msg, RTM_GETADDR, NLM_F_DUMP);
	rtnl_send_check();
}

static void request_route_dump(void)
{
	struct rtmsg* msg;
	nl_header(&rtnl, msg, RTM_GETROUTE, NLM_F_DUMP);
	rtnl_send_check();
}

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

static void set_link_carrier(struct link* ls, int carrier)
{
	int oldcarr = !!(ls->flags & F_CARRIER);

	if(oldcarr && carrier)
		return;
	if(!oldcarr && !carrier)
		return;

	if(carrier) {
		ls->flags |= F_CARRIER;
		eprintf("carrier acquired on %s\n", ls->name);
	} else {
		ls->flags &= ~F_CARRIER;
		eprintf("carrier lost on %s\n", ls->name);
	}
}

void msg_new_link(struct ifinfomsg* msg)
{
	struct link* ls;
	char* name;
	int nlen;
	uint8_t* u8;
	
	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return;
	if((nlen = strlen(name)) > sizeof(ls->name)-1)
		return;
	if(!(ls = grab_link_slot(msg->index)))
		return;

	if(!ls->ifi) {
		ls->ifi = msg->index;
		memcpy(ls->name, name, nlen);
		eprintf("new-link %i %s\n", msg->index, ls->name);
	}

	if((u8 = nl_u8(ifi_get(msg, IFLA_CARRIER))))
		set_link_carrier(ls, *u8);
}

void msg_del_link(struct ifinfomsg* msg)
{
	struct link* ls;

	if(!(ls = find_link_slot(msg->index)))
		return;

	eprintf("del-link %s %i\n", ls->ifi, ls->name);

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
	if(ls->flags & F_IPADDR)
		return;

	memcpy(ls->ip, ip, 4);
	ls->mask = msg->prefixlen;
	ls->flags |= F_IPADDR;

	eprintf("new-addr %s %i.%i.%i.%i/%i\n",
			ls->name,
			ip[0], ip[1], ip[2], ip[3],
			msg->prefixlen);
}

void msg_del_addr(struct ifaddrmsg* msg)
{
	struct link* ls;
	uint8_t* ip;

	if(!(ls = find_link_slot(msg->index)))
		return;
	if(!(ip = nl_bin(ifa_get(msg, IFA_ADDRESS), 4)))
		return;
	if(!(ls->flags & F_IPADDR))
		return;
	if(memcmp(ls->ip, ip, 4))
		return;

	memzero(ls->ip, 4);
	ls->mask = 0;
	ls->flags &= ~F_IPADDR;

	eprintf("del-addr %s %i.%i.%i.%i\n", ls->name,
			ip[0], ip[1], ip[2], ip[3]);
}

void msg_new_route(struct rtmsg* msg)
{
	struct link* ls;
	uint32_t* oif;
	uint8_t* gw;

	if(gateway.ifi)
		return;
	if(msg->type != RTN_UNICAST)
		return;
	if(msg->dst_len)
		return;
	if(!(oif = nl_u32(rtm_get(msg, RTA_OIF))))
		return;
	if(!(gw = nl_bin(rtm_get(msg, RTA_GATEWAY), 4)))
		return; /* not a gw route */
	if(!(ls = find_link_slot(*oif)))
		return;

	memcpy(gateway.ip, gw, 4);
	gateway.ifi = *oif;

	eprintf("new-gw %s %i.%i.%i.%i\n",
			ls->name, gw[0], gw[1], gw[2], gw[3]);
}

void msg_del_route(struct rtmsg* msg)
{
	struct link* ls;
	uint32_t* oif;
	uint8_t* gw;

	if(msg->dst_len)
		return;
	if(!(oif = nl_u32(rtm_get(msg, RTA_OIF))))
		return;
	if(!(gw = nl_bin(rtm_get(msg, RTA_GATEWAY), 4)))
		return;
	if(!(ls = find_link_slot(*oif)))
		return;
	if(gateway.ifi != *oif)
		return;
	if(memcmp(gateway.ip, gw, 4))
		return;

	memzero(&gateway, sizeof(gateway));

	eprintf("del-gw %s\n", ls->name);
}

/* At most one dump may be running at a time; requesting more results
   in EBUSY. Initially we need to run *three* of them, later del_addr
   and del_route may request their respective scans concurrently.
   To avoid errors, requests are serialized.

   NLMSG_DONE packets should never arrive unrequested on RTNL. */

static void proceed_with_dump(void)
{
	int bit;
	int req = rtnl_dump_pending;
	void (*func)(void) = NULL;

	if(!req)
		return;
	else if(req & (bit = DUMP_LINKS))
		func = request_link_dump;
	else if(req & (bit = DUMP_ADDRS))
		func = request_addr_dump;
	else if(req & (bit = DUMP_ROUTES))
		func = request_route_dump;
	if(!func)
		return;

	rtnl_dump_pending &= ~bit;
	rtnl_dump_lock = 1;

	func();
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

/* This RTNL so we only check packet header length. */

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
