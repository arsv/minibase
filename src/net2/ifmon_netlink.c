#include <bits/errno.h>
#include <bits/socket/inet.h>
#include <sys/signal.h>

#include <netlink.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <netlink/rtnl/mgrp.h>
#include <netlink/dump.h>

#include <string.h>
#include <printf.h>
#include <util.h>

#include "ifmon.h"

struct netlink nl;
int rtnlfd;

char txbuf[512];
char rxbuf[4096];

static void link_carrier_up(LS)
{
	tracef("link %s carrier up\n", ls->name);
}

static void link_carrier_down(LS)
{
	tracef("link %s carrier down\n", ls->name);
}

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

	ls->seq = nl.seq;
}

static void new_link_notification(struct ifinfomsg* msg)
{
	char* name;
	uint nlen;

	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return; /* skip unnamed links */
	if((nlen = strlen(name)) > IFNLEN)
		return; /* skip long names */

	spawn_identify(msg->index, name);
}

static void check_link_name(LS, struct ifinfomsg* msg)
{
	char* name;

	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return;
	
	uint nlen = strlen(name);
	uint olen = strlen(ls->name);

	if(nlen != olen)
		; /* length changed */
	else if(memcmp(name, ls->name, nlen))
		; /* same length, different content */
	else return;

	(void)update_link_name(ls);
}

static void link_state_changed(LS, struct ifinfomsg* msg)
{
	int oldflags = ls->flags;
	int newflags = oldflags;

	if(msg->flags & IFF_RUNNING)
		newflags |=  LF_CARRIER;
	else
		newflags &= ~LF_CARRIER;

	check_link_name(ls, msg);

	ls->flags = newflags;

	if(!(oldflags & LF_CARRIER) && (newflags & LF_CARRIER))
		link_carrier_up(ls);
	if((oldflags & LF_CARRIER) && !(newflags & LF_CARRIER))
		link_carrier_down(ls);
}

static void msg_new_link(struct ifinfomsg* msg)
{
	struct link* ls;

	if(msg->flags & (IFF_LOOPBACK | IFF_POINTOPOINT))
		return;

	int ifi = msg->index;

	tracef("msg_new_link seq %i\n", msg->nlm.seq);

	if((ls = find_link_slot(ifi)))
		link_state_changed(ls, msg);
	else if(!check_marked(ifi))
		new_link_notification(msg);
}

static void msg_del_link(struct ifinfomsg* msg)
{
	struct link* ls;
	int ifi = msg->index;
	int pid;

	unmark_link(ifi);

	if((ls = find_link_slot(ifi)))
		return;

	if((pid = ls->pid) > 0)
		sys_kill(pid, SIGTERM);

	free_link_slot(ls);
}

static struct link* find_link_seq(int seq)
{
	struct link* ls;
	int lq;

	for(ls = links; ls < links + nlinks; ls++)
		if((lq = ls->seq) && (lq == seq))
			return ls;

	return NULL;
}

static void msg_rtnl_err(struct nlerr* msg)
{
	struct link* ls;

	if((ls = find_link_seq(msg->seq))) {
		warn("rtnl", ls->name, msg->errno);
	} else {
		warn("rtnl", NULL, msg->errno);
	}
}

typedef void (*rth)(struct nlmsg* msg);

struct rtnh {
	int type;
	rth func;
	uint hdr;
} rtnlcmds[] = {
#define MSG(cmd, func, mm) { cmd, (rth)(func), sizeof(struct mm) }
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

	while((ret = nl_recv_nowait(&nl)) > 0) {
		while((msg = nl_get_nowait(&nl))) {
			dispatch(msg);
			nl_shift_rxbuf(&nl);
		}
	} if(ret < 0 && ret != -EAGAIN) {
		quit("recv", "rtnl", ret);
	}
}

void setup_netlink(void)
{
	struct ifinfomsg* req;

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));
	nl_connect(&nl, NETLINK_ROUTE, RTMGRP_LINK);

	nl_header(&nl, req, RTM_GETLINK, NLM_F_REQUEST | NLM_F_DUMP);
	send_check();

	rtnlfd = nl.fd;
}
