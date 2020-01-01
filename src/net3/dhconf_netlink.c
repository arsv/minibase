#include <bits/socket.h>
#include <bits/socket/inet.h>
#include <netlink.h>
#include <netlink/pack.h>
#include <netlink/recv.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <sys/socket.h>
#include <sys/creds.h>
#include <sys/file.h>

#include <string.h>
#include <endian.h>
#include <util.h>

#include "dhconf.h"

static void init_netlink(CTX)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW | SOCK_CLOEXEC;
	int protocol = NETLINK_ROUTE;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = 0
	};
	int fd, ret;

	if((fd = ctx->nlfd) > 0)
		quit(ctx, "double open for netlink", 0);
	if((fd = sys_socket(domain, type, protocol)) < 0)
		quit(ctx, "netlink socket", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		quit(ctx, "netlink bind", ret);

	ctx->nlfd = fd;
}

static void fini_netlink(CTX)
{
	int ret;

	if((ret = sys_close(ctx->nlfd)) < 0)
		quit(ctx, "netlink close", ret);

	ctx->nlfd = -1;
}

static int send_recv_ack(CTX, struct ncbuf* nc, uint seq)
{
	int fd = ctx->nlfd;
	struct nlmsg* msg;
	struct nlerr* err;
	byte buf[256];
	int ret;

	if((ret = nc_send(fd, nc)) < 0)
		return ret;
	if((ret = nl_recv(fd, buf, sizeof(buf))) < 0)
		return ret;
	if(!(msg = nl_msg(buf, ret)))
		return -EBADMSG;
	if(!(err = nl_err(msg)))
		return -EBADMSG;
	if(msg->seq != seq)
		return -EBADMSG;

	return err->errno;
}

static void put_lease_timing(struct ncbuf* nc, int lt, int rt)
{
	struct ifa_cacheinfo ci = {
		.valid = lt,
		.prefered = rt,
		.created = 0,
		.updated = 0
	};

	nc_put(nc, IFA_CACHEINFO, &ci, sizeof(ci));
}

static void add_ip_address(CTX)
{
	byte buf[200];
	struct ncbuf nc;
	struct ifaddrmsg* req;
	int ret, seq = ctx->seq++;
	int flags = NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;

	int mask = get_ctx_net_prefix_bits(ctx);
	int lt = ctx->lease_time;
	int rt = ctx->renew_time;

	nc_buf_set(&nc, buf, sizeof(buf));
	nc_header(&nc, RTM_NEWADDR, flags, seq);

	if(!(req = nc_fixed(&nc, sizeof(*req))))
		goto send;

	req->family = AF_INET;
	req->prefixlen = mask;
	req->flags = 0;
	req->scope = 0;
	req->index = ctx->ifindex;

	nc_put(&nc, IFA_LOCAL, ctx->ourip, 4);
	put_lease_timing(&nc, lt, rt);
send:
	if((ret = send_recv_ack(ctx, &nc, seq)) < 0)
		quit(ctx, "netlink RTM_NEWADDR", ret);
}

static void del_ip_address(CTX)
{
	byte buf[200];
	struct ncbuf nc;
	struct ifaddrmsg* req;
	int ret, seq = ctx->seq++;

	nc_buf_set(&nc, buf, sizeof(buf));
	nc_header(&nc, RTM_DELADDR, NLM_F_ACK, seq);

	if(!(req = nc_fixed(&nc, sizeof(*req))))
		goto send;

	req->family = AF_INET;
	req->prefixlen = get_ctx_net_prefix_bits(ctx);
	req->flags = 0;
	req->scope = 0;
	req->index = ctx->ifindex;

	nc_put(&nc, IFA_LOCAL, ctx->ourip, 4);
send:
	if((ret = send_recv_ack(ctx, &nc, seq)) >= 0)
		return;
	else if(ret != -EADDRNOTAVAIL)
		warn(NULL, "RTM_DELADDR", ret);
}

static void add_default_route(CTX)
{
	byte buf[256];
	struct ncbuf nc;
	struct rtmsg* req;
	uint8_t* gw;
	int ret, seq = ctx->seq++;
	int flags = NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;

	if(!(gw = get_ctx_opt(ctx, DHCP_ROUTER_IP, 4)))
		return;

	nc_buf_set(&nc, buf, sizeof(buf));
	nc_header(&nc, RTM_NEWROUTE, flags, seq);

	if(!(req = nc_fixed(&nc, sizeof(*req))))
		goto send;

	req->family = ARPHRD_EETHER; /* Important! EOPNOTSUPP if wrong. */
	req->dst_len = 0;
	req->src_len = 0;
	req->tos = 0;
	req->table = RT_TABLE_MAIN;
	req->protocol = RTPROT_DHCP;
	req->scope = RT_SCOPE_UNIVERSE;
	req->type = RTN_UNICAST;
	req->flags = 0;

	nc_put_int(&nc, RTA_OIF, ctx->ifindex);
	nc_put(&nc, RTA_GATEWAY, gw, 4);
send:
	if((ret = send_recv_ack(ctx, &nc, seq)) >= 0)
		return;
	else if(ret != -EEXIST)
		quit(ctx, "netlink RTM_NEWROUTE", ret);
}

void config_iface(CTX)
{
	init_netlink(ctx);
	add_ip_address(ctx);
	add_default_route(ctx);
	fini_netlink(ctx);
}

void update_iface(CTX)
{
	init_netlink(ctx);
	add_ip_address(ctx);
	fini_netlink(ctx);
}

void deconf_iface(CTX)
{
	init_netlink(ctx);
	del_ip_address(ctx);
	fini_netlink(ctx);
}
