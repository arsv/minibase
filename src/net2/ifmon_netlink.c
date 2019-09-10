#include <bits/errno.h>
#include <bits/socket/inet.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/creds.h>

#include <netlink.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <netlink/rtnl/mgrp.h>
#include <netlink/dump.h>

#include <string.h>
#include <util.h>

#include "ifmon.h"

char rxbuf[4096];

static void enabled_rise(CTX, LS)
{
	ls->flags |= LF_NEED_POKE | LF_MARKED;
}

static void carrier_rise(CTX, LS)
{
	int flags = ls->flags;

	if(!(flags & LF_AUTO_DHCP))
		return;

	ls->flags = flags | LF_NEED_DHCP | LF_MARKED;
}

static void carrier_fall(CTX, LS)
{
	ls->flags &= ~LF_NEED_DHCP;

	sighup_running_dhcp(ls);
}

void simulate_reconnect(CTX, LS)
{
	carrier_fall(ctx, ls);
	carrier_rise(ctx, ls);
}

struct link* find_link_slot(CTX, int ifi)
{
	struct link* ls;
	struct link* links = ctx->links;
	int nlinks = ctx->nlinks;

	for(ls = links; ls < links + nlinks; ls++)
		if(ls->ifi == ifi)
			return ls;

	return NULL;
}

struct link* grab_link_slot(CTX)
{
	struct link* ls;
	struct link* links = ctx->links;
	int nlinks = ctx->nlinks;

	for(ls = links; ls < links + nlinks; ls++)
		if(!ls->ifi)
			return ls;
	if(nlinks >= NLINKS)
		return NULL;

	ctx->nlinks = nlinks + 1;

	return &links[nlinks];
}

void free_link_slot(CTX, struct link* ls)
{
	struct link* links = ctx->links;
	int n = ls - links;

	memzero(ls, sizeof(*ls));

	if(n != ctx->nlinks - 1)
		return;

	while(--n >= 0)
		if(links[n].ifi)
			break;

	ctx->nlinks = n;
}

static struct nlattr* ifi_get(struct ifinfomsg* msg, int key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static void new_link_notification(CTX, struct ifinfomsg* msg)
{
	struct link* ls;
	char* name;
	uint nlen;

	int ifi = msg->index;

	if(!(name = nl_str(ifi_get(msg, IFLA_IFNAME))))
		return; /* skip unnamed links */
	if((nlen = strlen(name)) > IFNLEN)
		return; /* skip long names */

	if(!(ls = grab_link_slot(ctx)))
		return; /* out of slots */

	ls->ifi = ifi;
	memcpy(ls->name, name, nlen);

	if(msg->flags & IFF_UP)
		ls->flags |= LF_ENABLED;
	if(msg->flags & IFF_RUNNING)
		ls->flags |= LF_CARRIER;

	spawn_identify(ctx, ls);
}

static void check_link_name(CTX, LS, struct ifinfomsg* msg)
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

	(void)update_link_name(ctx, ls);
}

static void link_state_changed(CTX, LS, struct ifinfomsg* msg)
{
	int msgflags = msg->flags;
	int oldflags = ls->flags;
	int newflags = oldflags;

	if(msgflags & IFF_RUNNING)
		newflags |=  LF_CARRIER;
	else
		newflags &= ~LF_CARRIER;

	if(msgflags & IFF_UP)
		newflags |=  LF_ENABLED;
	else
		newflags &= ~LF_ENABLED;

	check_link_name(ctx, ls, msg);

	ls->flags = newflags;

	if(!(oldflags & LF_ENABLED) && (newflags & LF_ENABLED))
		enabled_rise(ctx, ls);
	if(!(oldflags & LF_CARRIER) && (newflags & LF_CARRIER))
		carrier_rise(ctx, ls);
	if((oldflags & LF_CARRIER) && !(newflags & LF_CARRIER))
		carrier_fall(ctx, ls);
}

static void msg_new_link(CTX, struct ifinfomsg* msg)
{
	struct link* ls;

	if(msg->flags & (IFF_LOOPBACK | IFF_POINTOPOINT))
		return;

	int ifi = msg->index;

	if((ls = find_link_slot(ctx, ifi)))
		link_state_changed(ctx, ls, msg);
	else
		new_link_notification(ctx, msg);
}

static void msg_del_link(CTX, struct ifinfomsg* msg)
{
	struct link* ls;
	int ifi = msg->index;

	if((ls = find_link_slot(ctx, ifi)))
		return;

	if(ls->flags & LF_RUNNING)
		sys_kill(ls->pid, SIGTERM);

	free_link_slot(ctx, ls);
}

static void msg_rtnl_err(struct nlerr* msg)
{
	warn("rtnl", NULL, msg->errno);
}

typedef void (*rth)(CTX, struct nlmsg* msg);

static const struct rtnh {
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

static void dispatch(CTX, struct nlmsg* msg)
{
	const struct rtnh* rh;

	for(rh = rtnlcmds; rh->hdr; rh++)
		if(msg->type == rh->type)
			break;
	if(!rh->hdr)
		return;
	if(msg->len < rh->hdr)
		return;
	if(!rh->func)
		return;

	rh->func(ctx, msg);
}

void handle_rtnl(CTX)
{
	struct nlmsg* msg;
	int fd = ctx->rtnlfd;
	int rd;
	int off = 0;
	void* buf = rxbuf;
	int max = sizeof(rxbuf);
again:
	if((rd = sys_recv(fd, buf + off, max - off, 0)) < 0) {
		if(rd != -EAGAIN)
			quit("recv", "NETLINK", rd);
		else if(off)
			quit("recv", "NETLINK", -ENOBUFS);
		else
			return;
	} if(rd == 0) {
		quit("EOF", "NETLINK", 0);
	}

	int len = off + rd;
	void* p = buf;
	void* e = buf + len;

	while(p < e) {
		if(!(msg = nl_msg(p, e - p)))
			break;

		dispatch(ctx, msg);

		p += nl_len(msg);
	}

	if(p >= e)
		return;
	if(p == buf)
		quit("recv", "NETLINK", -ENOBUFS);

	off = e - p;
	memmove(buf, p, off);

	goto again;
}

static int connect_netlink(void)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC;
	int protocol = NETLINK_ROUTE;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = RTMGRP_LINK
	};
	int fd, ret;

	if((fd = sys_socket(domain, type, protocol)) < 0)
		quit("socket", "NETLINK", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		quit("bind", "NETLINK", ret);

	return fd;
}

static void trigger_link_dump(int fd)
{
	struct ifinfomsg req = {
		.nlm = {
			.len = sizeof(req),
			.type = RTM_GETLINK,
			.flags = NLM_F_REQUEST | NLM_F_DUMP,
			.seq = 0,
			.pid = 0
		},
		.index = 0
	};

	void* buf = &req;
	int len = sizeof(req);
	int ret;

	if((ret = sys_send(fd, buf, len, 0)) < 0)
		quit("send", "NETLINK", ret);
}

void setup_netlink(CTX)
{
	int fd = connect_netlink();

	trigger_link_dump(fd);

	ctx->rtnlfd = fd;
}
