#include <netlink.h>
#include <netlink/dump.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/mgrp.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "wimon.h"

struct netlink rtnl;

char rtnl_tx[512];
char rtnl_rx[4096];

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

void handle_new_link(struct nlmsg* nlm)
{
	struct link* ls;
	struct ifinfomsg* msg;
	char* name;
	int nlen;
	
	if(!(msg = nl_ifi(nlm)))
		return;
	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return;

	if((nlen = strlen(name)) > sizeof(ls->name)-1)
		return;
	if(!(ls = grab_link_slot(msg->index)))
		return;

	ls->ifi = msg->index;
	memset(ls->name, 0, sizeof(ls->name));
	memcpy(ls->name, name, nlen);
	
	eprintf("NEWLINK %i %s\n", msg->index, name);
}

void handle_del_link(struct nlmsg* nlm)
{
	struct link* ls;
	struct ifinfomsg* msg;

	if(!(msg = nl_ifi(nlm)))
		return;
	if(!(ls = find_link_slot(msg->index)))
		return;

	eprintf("DELLINK %i %s\n", ls->ifi, ls->name);

	free_link_slot(ls);
}

void warn_rtnl_err(struct nlmsg* msg)
{
	struct nlerr* err;
	
	if(!(err = nl_err(msg)))
		return;

	warn("rtnl", NULL, err->errno);
}

void handle_rtnl(struct nlmsg* msg)
{
	switch(msg->type) {
		case NLMSG_NOOP:
		case NLMSG_DONE: break;
		case RTM_NEWLINK: handle_new_link(msg); break;
		case RTM_DELLINK: handle_del_link(msg); break;
		case NLMSG_ERROR: warn_rtnl_err(msg); break;
		default: nl_dump_rtnl(msg);
	}
}

void request_link_list(void)
{
	struct ifinfomsg* msg;

	nl_header(&rtnl, msg, RTM_GETLINK, NLM_F_DUMP,
		.family = 0,
		.type = 0,
		.index = 0,
		.flags = 0,
		.change = 0);

	if(nl_send(&rtnl))
		fail("send", "rtnl", rtnl.err);
}

void setup_rtnl(void)
{
	int mgrp_link = RTMGRP_LINK | RTMGRP_NOTIFY;
	int mgrp_ipv4 = RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;

	nl_init(&rtnl);
	nl_set_txbuf(&rtnl, rtnl_tx, sizeof(rtnl_tx));
	nl_set_rxbuf(&rtnl, rtnl_rx, sizeof(rtnl_rx));
	nl_connect(&rtnl, NETLINK_ROUTE, mgrp_link | mgrp_ipv4);

	request_link_list();
}
