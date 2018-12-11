#include <bits/errno.h>
#include <bits/socket/inet.h>

#include <netlink.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <netlink/rtnl/mgrp.h>
#include <netlink/dump.h>

#include <string.h>
#include <util.h>

#include "ifmon.h"

struct netlink nl;
int netlink;

char txbuf[512];
char rxbuf[4096];

static void send_check(void)
{
	if(nl_send(&nl) >= 0)
		return;

	quit("netlink", NULL, nl.err);
}

static struct nlattr* ifi_get(struct ifinfomsg* msg, int key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

void request_link_name(struct link* ls)
{
	struct ifinfomsg* msg;

	nl_header(&nl, msg, RTM_GETLINK, 0,
		.family = 0,
		.type = 0,
		.index = ls->ifi,
		.flags = 0,
		.change = 0);

	send_check();
}

static void new_link_notification(struct ifinfomsg* msg)
{
	char* name;
	uint nlen;

	/* Skip unnamed links; should never happen but who knows. */
	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return;
	if((nlen = strlen(name)) > IFNLEN-1)
		return warn("ignoring long name link", name, 0);

	spawn_identify(msg->index, name);
}

static void check_link_name(LS, struct ifinfomsg* msg)
{
	char* name;

	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return;

	uint nlen = strlen(name);
	uint olen = strlen(ls->name);

	if(olen == nlen && !memcmp(ls->name, name, nlen))
		ls->flags &= ~LF_MISNAMED;
	else
		ls->flags |=  LF_MISNAMED;
}

static void link_state_changed(LS, struct ifinfomsg* msg)
{
	int hadcarrier = (ls->flags & LF_CARRIER);
	int gotcarrier = (msg->flags & IFF_RUNNING);

	if(!hadcarrier && gotcarrier) {
		ls->flags |= LF_CARRIER;

		if(ls->flags & LF_DHCP)
			ls->needs |= LN_REQUEST;
	} else if(hadcarrier && !gotcarrier) {
		ls->flags &= ~LF_CARRIER;
		ls->needs &= ~LN_REQUEST;

		if(ls->flags & LF_REQUEST)
			kill_all_procs(ls);
		else if(ls->flags & LF_DISCONT)
			ls->needs |= LN_CANCEL;

		if(ls->flags & LF_ONCE)
			ls->flags &= ~(LF_DHCP | LF_ONCE);
	}

	check_link_name(ls, msg);

	reassess_link(ls);
}

static void msg_new_link(struct ifinfomsg* msg)
{
	struct link* ls;

	if(msg->flags & (IFF_LOOPBACK | IFF_POINTOPOINT))
		return;

	int ifi = msg->index;

	if(!check_marked(ifi))
		new_link_notification(msg);
	else if((ls = find_link_slot(ifi)))
		link_state_changed(ls, msg);
}

static void msg_del_link(struct ifinfomsg* msg)
{
	struct link* ls;
	int ifi = msg->index;

	unmark_link(ifi);

	if((ls = find_link_slot(ifi)))
		return;

	kill_all_procs(ls);
	free_link_slot(ls);
}


static void* ifa_bin(struct ifaddrmsg* msg, int key, int size)
{
	return nl_bin(nl_attr_k_in(NLPAYLOAD(msg), key), size);
}

static void msg_new_addr(struct ifaddrmsg* msg)
{
	struct link* ls;
	struct ifa_cacheinfo* ci;
	const uint32_t unset = ~((uint32_t)0);

	if(!(ls = find_link_slot(msg->index)))
		return;
	if(!(ls->flags & LF_DHCP))
		return;
	if(!(ci = ifa_bin(msg, IFA_CACHEINFO, sizeof(*ci))))
		return;
	if(ci->valid == unset)
		return;
	if(ci->prefered || !ci->valid)
		return;

	ls->needs |= LN_RENEW;
	reassess_link(ls);
}

static void link_error(LS, int errno)
{
	warn("rtnl", ls->name, errno);
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

		return link_error(ls, msg->errno);
	}

	warn("rtnl", NULL, msg->errno);
}

static void trigger_link_dump(void)
{
	struct ifinfomsg* req;

	nl_header(&nl, req, RTM_GETLINK, NLM_F_REQUEST | NLM_F_DUMP);

	send_check();
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
	MSG(RTM_NEWADDR,  msg_new_addr,  ifaddrmsg),
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

	if((ret = nl_recv_nowait(&nl)) < 0) {
		warn("recv", "rtnl", ret);
		return;
	}
	while((msg = nl_get_nowait(&nl)))
		dispatch(msg);

	nl_shift_rxbuf(&nl);
}

void setup_rtnl(void)
{
	int mgrp_link = RTMGRP_LINK | RTMGRP_NOTIFY;
	int mgrp_ipv4 = RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));
	nl_connect(&nl, NETLINK_ROUTE, mgrp_link | mgrp_ipv4);

	trigger_link_dump();

	netlink = nl.fd;
}
