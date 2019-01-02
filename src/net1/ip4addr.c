#include <bits/socket/inet.h>
#include <bits/errno.h>

#include <netlink.h>
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

char txbuf[1024];
char rxbuf[3*1024];

struct top {
	int argc;
	int argi;
	char** argv;

	int opts;
	uint ifi;

	struct netlink nl;
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

static void flush_iface(CTX)
{
	struct netlink* nl = &ctx->nl;
	struct ifaddrmsg* req;

	nl_header(nl, req, RTM_DELADDR, 0,
		.family = AF_INET,
		.prefixlen = 0,
		.flags = 0,
		.scope = 0,
		.index = ctx->ifi);

	long ret = nl_send_recv_ack(nl);

	while((ret = nl_send_recv_ack(nl)) >= 0)
		;
	if(ret != -EADDRNOTAVAIL)
		fail("netlink", "RTM_DELADDR", nl->err);
}

static void set_iface_state(CTX, int up)
{
	struct netlink* nl = &ctx->nl;
	struct ifinfomsg* req;

	nl_header(nl, req, RTM_NEWLINK, 0,
		.family = AF_INET,
		.type = 0,
		.index = ctx->ifi,
		.flags = up ? IFF_UP : 0,
		.change = IFF_UP);

	if(nl_send_recv_ack(nl))
		fail("netlink", "RTM_NEWLINK", nl->err);
}

static void set_iface_address(CTX, uint8_t ipm[5])
{
	struct netlink* nl = &ctx->nl;
	struct ifaddrmsg* req;

	nl_header(nl, req, RTM_NEWADDR, NLM_F_REPLACE,
		.family = AF_INET,
		.prefixlen = ipm[4],
		.flags = 0,
		.scope = 0,
		.index = ctx->ifi);
	nl_put(nl, IFA_LOCAL, ipm, 4);

	if(nl_send_recv_ack(nl))
		fail("netlink", "RTM_NEWADDR", nl->err);
}

static void setup_netlink(CTX, char* ifname)
{
	struct netlink* nl = NL;
	int ifi, ret;

	nl_init(nl);

	nl_set_rxbuf(nl, rxbuf, sizeof(rxbuf));
	nl_set_txbuf(nl, txbuf, sizeof(txbuf));

	if((ret = nl_connect(nl, NETLINK_ROUTE, 0)) < 0)
		fail("netlink connect", NULL, ret);
	if(!ifname)
		fail("need interface name", NULL, 0);
	if((ifi = getifindex(nl->fd, ifname)) <= 0)
		fail("unknown interface", ifname, 0);

	ctx->ifi = ifi;
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

	memzero(ctx, sizeof(*ctx));

	parse_args(ctx, argc, argv);

	setup_netlink(ctx, shift_arg(ctx));

	check_parse_ipmask(ip, shift_arg(ctx));
	flush_iface(ctx);
	set_iface_state(ctx, 1);
	set_iface_address(ctx, ip);

	return 0;
}
