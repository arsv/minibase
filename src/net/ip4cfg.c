#include <bits/socket/inet.h>
#include <bits/errno.h>

#include <netlink.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/route.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <fail.h>

ERRTAG = "ip4cfg";
ERRLIST = {
	REPORT(EPERM), REPORT(EBUSY), REPORT(ENOENT), REPORT(EBADF),
	REPORT(ENETDOWN), REPORT(EEXIST), REPORT(EOPNOTSUPP),
	REPORT(ECONNREFUSED), REPORT(EFAULT), REPORT(EINTR), REPORT(EINVAL),
	REPORT(ENOMEM), REPORT(ENOTCONN), REPORT(ENOTSOCK), REPORT(EBADMSG),
	REPORT(EAFNOSUPPORT), REPORT(ENOBUFS), REPORT(EPROTONOSUPPORT),
	RESTASNUMBERS
};

#define OPTS "d"
#define OPT_d (1<<0)

char txbuf[1024];
char rxbuf[3*1024];

static int gotall(char* p)
{
	return (p && !*p);
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

static void need_one_more_arg(int i, int argc)
{
	if(i >= argc)
		fail("too few arguments", NULL, 0);
}

static void need_no_more_args(int i, int argc)
{
	if(i < argc)
		fail("too many arguments", NULL, 0);
}

static void flush_iface(struct netlink* nl, int idx)
{
	struct ifaddrmsg* req;

	nl_header(nl, req, RTM_DELADDR, 0,
		.family = AF_INET,
		.prefixlen = 0,
		.flags = 0,
		.scope = 0,
		.index = idx);

	long ret = nl_send_recv_ack(nl);

	if(ret && ret != -EADDRNOTAVAIL)
		fail("netlink", "RTM_DELADDR", nl->err);
}

static void set_iface_state(struct netlink* nl, int ifi, int up)
{
	struct ifinfomsg* req;

	nl_header(nl, req, RTM_NEWLINK, 0,
		.family = AF_INET,
		.type = 0,
		.index = ifi,
		.flags = up ? IFF_UP : 0,
		.change = IFF_UP);

	if(nl_send_recv_ack(nl))
		fail("netlink", "RTM_NEWLINK", nl->err);
}

static void set_iface_address(struct netlink* nl, int ifi, uint8_t ipm[5])
{
	struct ifaddrmsg* req;

	nl_header(nl, req, RTM_NEWADDR, NLM_F_REPLACE,
		.family = AF_INET,
		.prefixlen = ipm[4],
		.flags = 0,
		.scope = 0,
		.index = ifi);
	nl_put(nl, IFA_LOCAL, ipm, 4);

	if(nl_send_recv_ack(nl))
		fail("netlink", "RTM_NEWADDR", nl->err);
}

static void deconf(struct netlink* nl, int ifi, int i, int argc, char** argv)
{
	need_no_more_args(i, argc);
	set_iface_state(nl, ifi, 0);
	flush_iface(nl, ifi);
}

static void add_default_route(struct netlink* nl, int ifi, uint8_t gw[4])
{
	struct rtmsg* req;
	uint32_t oif = ifi;

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

static void config(struct netlink* nl, int ifi, int i, int argc, char** argv)
{
	uint8_t ip[5];
	uint8_t gw[4];

	need_one_more_arg(i, argc);
	check_parse_ipmask(ip, argv[i++]);

	flush_iface(nl, ifi);
	set_iface_state(nl, ifi, 1);
	set_iface_address(nl, ifi, ip);

	if(i < argc && !strcmp("gw", argv[i])) {
		need_one_more_arg(i++, argc);
		check_parse_ipaddr(gw, argv[i++]);
		add_default_route(nl, ifi, gw);
	}
}

static void setup(struct netlink* nl, int* ifi, char* name)
{
	nl_init(nl);

	nl_set_rxbuf(nl, rxbuf, sizeof(rxbuf));
	nl_set_txbuf(nl, txbuf, sizeof(txbuf));

	xchk(nl_connect(nl, NETLINK_ROUTE, 0),
			"netlink connect", NULL);

	if((*ifi = getifindex(nl->fd, name)) <= 0)
		fail("unknown interface", name, 0);
}

int main(int argc, char** argv)
{
	struct netlink nl;
	int ifindex;

	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	need_one_more_arg(i, argc);
	setup(&nl, &ifindex, argv[i++]);

	if(opts & OPT_d)
		deconf(&nl, ifindex, i, argc, argv);
	else
		config(&nl, ifindex, i, argc, argv);

	return 0;
}
