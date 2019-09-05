#include <bits/socket.h>
#include <bits/socket/inet.h>
#include <netlink.h>
#include <netlink/cmd.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <bits/ioctl/socket.h>
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

static int send_recv_ack(CTX, struct ncbuf* nc, int seq)
{
	int fd = ctx->nlfd;
	byte buf[256];
	int ret;

	if((ret = nc_send(fd, nc)) < 0)
		return ret;
	if((ret = nc_recv_ack(fd, buf, sizeof(buf), seq)) < 0)
		return ret;

	return 0;
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

	int mask = get_ctx_net_prefix_bits(ctx);
	int lt = ctx->lease_time;
	int rt = ctx->renew_time;

	if(!(req = nc_struct(&nc, buf, sizeof(buf), sizeof(*req))))
		quit(ctx, "netlink RTM_NEWADDR", -ENOBUFS);

	int flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_REPLACE;

	nc_header(&req->nlm, RTM_NEWADDR, flags, seq);

	req->family = AF_INET;
	req->prefixlen = mask;
	req->flags = 0;
	req->scope = 0;
	req->index = ctx->ifindex;

	nc_put(&nc, IFA_LOCAL, ctx->ourip, 4);
	put_lease_timing(&nc, lt, rt);

	nc_length(&req->nlm, &nc);

	if((ret = send_recv_ack(ctx, &nc, seq)) < 0)
		quit(ctx, "netlink RTM_NEWADDR", ret);
}

static void del_ip_address(CTX)
{
	byte buf[200];
	struct ncbuf nc;
	struct ifaddrmsg* req;
	int ret, seq = ctx->seq++;

	if(!(req = nc_struct(&nc, buf, sizeof(buf), sizeof(*req))))
		quit(ctx, "netlink RTM_DELADDR", -ENOBUFS);

	nc_header(&req->nlm, RTM_DELADDR, NLM_F_REQUEST | NLM_F_ACK, seq);

	req->family = AF_INET;
	req->prefixlen = get_ctx_net_prefix_bits(ctx);
	req->flags = 0;
	req->scope = 0;
	req->index = ctx->ifindex;

	nc_put(&nc, IFA_LOCAL, ctx->ourip, 4);

	nc_length(&req->nlm, &nc);

	ret = send_recv_ack(ctx, &nc, seq);

	if(ret < 0 && ret != -EADDRNOTAVAIL)
		warn(NULL, "RTM_DELADDR", ret);
}

static void add_default_route(CTX)
{
	byte buf[256];
	struct ncbuf nc;
	struct rtmsg* req;
	uint8_t* gw;
	int seq = ctx->seq++;

	if(!(gw = get_ctx_opt(ctx, DHCP_ROUTER_IP, 4)))
		return;

	if(!(req = nc_struct(&nc, buf, sizeof(buf), sizeof(*req))))
		quit(ctx, "netlink RTM_NEWROUTE", -ENOBUFS);

	int flags = NLM_F_REQUEST | NLM_F_ACK | NLM_F_CREATE | NLM_F_EXCL;

	nc_header(&req->nlm, RTM_NEWROUTE, flags, seq);

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

	nc_length(&req->nlm, &nc);

	int ret = send_recv_ack(ctx, &nc, seq);

	if(ret < 0 && ret != -EEXIST)
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
