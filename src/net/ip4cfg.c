#include <bits/socket/inet.h>
#include <bits/errno.h>

#include <netlink.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/route.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <util.h>

ERRTAG("ip4cfg");

#define OPTS "du"
#define OPT_d (1<<0)
#define OPT_u (1<<1)

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

static int match_next_arg(CTX, char* kw)
{
	if(ctx->argi >= ctx->argc)
		return 0;
	if(strcmp(ctx->argv[ctx->argi], kw))
		return 0;

	ctx->argi++;

	return 1;
}

static void check_parse_ipmask(uint8_t ip[5], char* arg)
{
	if(!gotall(parseipmask(arg, ip, ip + 4)))
		fail("invalid address", arg, 0);
}

static void check_parse_ipaddr(uint8_t ip[4], char* arg)
{
	if(!gotall(parseip(arg, ip)))
		fail("invalid address", arg, 0);
}

static void no_more_args(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
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

static void deconf(CTX)
{
	no_more_args(ctx);
	set_iface_state(ctx, 0);
	flush_iface(ctx);
}

static void add_default_route(CTX, uint8_t gw[4])
{
	struct netlink* nl = &ctx->nl;
	struct rtmsg* req;
	uint32_t oif = ctx->ifi;

	nl_header(nl, req, RTM_NEWROUTE, NLM_F_CREATE | NLM_F_EXCL,
		.family = ARPHRD_EETHER, /* Important! EOPNOTSUPP if wrong. */
		.dst_len = 0,
		.src_len = 0,
		.tos = 0,
		.table = RT_TABLE_MAIN,
		.protocol = RTPROT_STATIC,
		.scope = RT_SCOPE_UNIVERSE,
		.type = RTN_UNICAST,
		.flags = 0);
	nl_put(nl, RTA_OIF, &oif, sizeof(oif));
	nl_put(nl, RTA_GATEWAY, gw, 4);

	if(nl_send_recv_ack(nl))
		fail("netlink", "RTM_NEWROUTE", nl->err);
}

static void config(CTX)
{
	uint8_t ip[5];
	uint8_t gw[4];

	check_parse_ipmask(ip, shift_arg(ctx));

	flush_iface(ctx);
	set_iface_state(ctx, 1);
	set_iface_address(ctx, ip);

	if(match_next_arg(ctx, "gw")) {
		check_parse_ipaddr(gw, shift_arg(ctx));
		add_default_route(ctx, gw);
	}
}

static void bringup(CTX)
{
	no_more_args(ctx);
	set_iface_state(ctx, 1);
}

static void setup_netlink(CTX, char* ifname)
{
	struct netlink* nl = NL;
	int ifi;

	nl_init(nl);

	nl_set_rxbuf(nl, rxbuf, sizeof(rxbuf));
	nl_set_txbuf(nl, txbuf, sizeof(txbuf));

	xchk(nl_connect(nl, NETLINK_ROUTE, 0),
			"netlink connect", NULL);

	if(!ifname)
		fail("need interface name", NULL, 0);
	if((ifi = getifindex(nl->fd, ifname)) <= 0)
		fail("unknown interface", ifname, 0);

	ctx->ifi = ifi;
}

static int parse_args(CTX, int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->argc = argc;
	ctx->argi = i;
	ctx->argv = argv;
	ctx->opts = opts;

	setup_netlink(ctx, shift_arg(ctx));

	return opts;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	int opts = parse_args(ctx, argc, argv);

	if(opts & OPT_d)
		deconf(ctx);
	else if(opts & OPT_u)
		bringup(ctx);
	else
		config(ctx);

	return 0;
}
