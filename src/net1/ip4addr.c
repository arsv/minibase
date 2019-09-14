#include <bits/socket/inet.h>
#include <bits/errno.h>
#include <sys/socket.h>
#include <sys/creds.h>

#include <netlink.h>
#include <netlink/pack.h>
#include <netlink/recv.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/route.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("ip4addr");

#define OPTS "duf"
#define OPT_d (1<<0)
#define OPT_u (1<<1)
#define OPT_f (1<<2)

struct top {
	int argc;
	int argi;
	char** argv;

	int opts;
	uint ifi;

	int nl;
	struct ncbuf nc;
};

#define CTX struct top* ctx
#define NL &ctx->nl

static int gotall(char* p)
{
	return (p && !*p);
}

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		fail("too few arguments", NULL, 0);

	return ctx->argv[ctx->argi++];
}

static void check_parse_ipmask(uint8_t ip[5], char* arg)
{
	if(!gotall(parseipmask(arg, ip, ip + 4)))
		fail("invalid address", arg, 0);
}

static int send_recv_ack(CTX, struct ncbuf* nc, int seq)
{
	int fd = ctx->nl;
	byte buf[64];
	struct nlmsg* msg;
	struct nlerr* err;
	int ret;

	if((ret = nc_send(fd, nc)) < 0)
		fail("send", "NETLINK", ret);
	if((ret = nl_recv(fd, buf, sizeof(buf))) < 0)
		fail("recv", "NETLINK", ret);
	if(!(msg = nl_msg(buf, ret)))
		fail("recv", "NETLINK", -EBADMSG);
	if(msg->seq != seq)
		fail("recv", "NETLINK", -EBADMSG);
	if(!(err = nl_err(msg)))
		fail("recv", "NETLINK", -EBADMSG);

	return err->errno;
}

static void flush_iface(CTX)
{
	struct ncbuf* nc = &ctx->nc;
	struct ifaddrmsg* req;
	int ret;

	nc_header(nc, RTM_DELADDR, NLM_F_ACK, 0);

	if(!(req = nc_fixed(nc, sizeof(*req))))
		goto send;

	req->family = AF_INET;
	req->index = ctx->ifi;
send:
	if((ret = send_recv_ack(ctx, nc, 0)) >= 0)
		goto send;
	else if(ret != -EADDRNOTAVAIL)
		fail(NULL, "DELADDR", ret);
}

static void set_iface_state(CTX, int up)
{
	struct ncbuf* nc = &ctx->nc;
	struct ifinfomsg* req;
	int ret;

	nc_header(nc, RTM_NEWLINK, NLM_F_ACK, 0);

	if(!(req = nc_fixed(nc, sizeof(*req))))
		goto send;

	req->family = AF_INET;
	req->index = ctx->ifi;
	req->flags = up ? IFF_UP : 0;
	req->change = IFF_UP;
send:
	if((ret = send_recv_ack(ctx, nc, 0)) < 0)
		fail(NULL, "NEWLINK", ret);
}

static void set_iface_address(CTX, uint8_t ipm[5])
{
	struct ncbuf* nc = &ctx->nc;
	struct ifaddrmsg* req;
	int ret;

	nc_header(nc, RTM_NEWADDR, NLM_F_ACK, 0);

	if(!(req = nc_fixed(nc, sizeof(*req))))
		goto send;

	req->family = AF_INET,
	req->prefixlen = ipm[4],
	req->index = ctx->ifi;

	nc_put(nc, IFA_LOCAL, ipm, 4);
send:
	if((ret = send_recv_ack(ctx, nc, 0)) < 0)
		fail(NULL, "NEWADDR", ret);
}

static void setup_netlink(CTX, char* ifname)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW | SOCK_CLOEXEC;
	int protocol = NETLINK_ROUTE;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = 0
	};
	int fd, ifi, ret;

	if((fd = sys_socket(domain, type, protocol)) < 0)
		fail("socket", "NETLINK", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		fail("bind", "NETLINK", ret);

	if(!ifname)
		fail("need interface name", NULL, 0);
	if((ifi = getifindex(fd, ifname)) <= 0)
		fail("unknown interface", ifname, 0);

	ctx->ifi = ifi;
	ctx->nl = fd;
}

static void parse_args(CTX, int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->argc = argc;
	ctx->argi = i;
	ctx->argv = argv;
	ctx->opts = opts;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	uint8_t ip[5];
	byte buf[64];

	memzero(ctx, sizeof(*ctx));

	parse_args(ctx, argc, argv);

	nc_buf_set(&ctx->nc, buf, sizeof(buf));

	setup_netlink(ctx, shift_arg(ctx));

	check_parse_ipmask(ip, shift_arg(ctx));
	flush_iface(ctx);
	set_iface_state(ctx, 1);
	set_iface_address(ctx, ip);

	return 0;
}
