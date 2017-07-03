#include <bits/errno.h>
#include <bits/socket/inet.h>
#include <netlink.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>

#include <endian.h>
#include <fail.h>

#include "dhcp.h"

char rxbuf[512];
char txbuf[512];

struct netlink nl;

static void init_netlink(void)
{
	int ret;

	nl_init(&nl);
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));

	if((ret = nl_connect(&nl, NETLINK_ROUTE, 0)) < 0)
		fail("connect", "NETLINK_ROUTE", ret);
}

static void set_iface_address(int ifi, uint8_t ip[4], int mask, int lt, int rt)
{
	struct ifaddrmsg* req;

	nl_header(&nl, req, RTM_NEWADDR, NLM_F_REPLACE,
		.family = AF_INET,
		.prefixlen = mask,
		.flags = 0,
		.scope = 0,
		.index = ifi);
	nl_put(&nl, IFA_LOCAL, ip, 4);

	if(rt > lt) /* renew time must be less than lease time */
		rt = 0;
	if(!rt)
		rt = lt/2;
	if(lt) {
		struct ifa_cacheinfo ci = {
			.valid = lt,
			.prefered = rt,
			.created = 0,
			.updated = 0
		};
		nl_put(&nl, IFA_CACHEINFO, &ci, sizeof(ci));
	}

	if(nl_send_recv_ack(&nl))
		fail("RTM_NEWADDR", NULL, nl.err);
}

static void add_default_route(int ifi, uint8_t gw[4])
{
	struct rtmsg* req;
	uint32_t oif = ifi;

	nl_header(&nl, req, RTM_NEWROUTE, NLM_F_CREATE | NLM_F_EXCL,
		.family = ARPHRD_EETHER, /* Important! EOPNOTSUPP if wrong. */
		.dst_len = 0,
		.src_len = 0,
		.tos = 0,
		.table = RT_TABLE_MAIN,
		.protocol = RTPROT_DHCP,
		.scope = RT_SCOPE_UNIVERSE,
		.type = RTN_UNICAST,
		.flags = 0);
	nl_put(&nl, RTA_OIF, &oif, sizeof(oif));
	nl_put(&nl, RTA_GATEWAY, gw, 4);

	int ret = nl_send_recv_ack(&nl);

	if(ret && ret != -EEXIST)
		fail("RTM_NEWROUTE", NULL, ret);
}

static int maskbits(void)
{
	struct dhcpopt* opt;
	int mask = 0;
	int i, b;

	if(!(opt = get_option(DHCP_NETMASK, 4)))
		return 0;

	uint8_t* ip = (uint8_t*)opt->payload;

	for(i = 3; i >= 0; i--) {
		for(b = 0; b < 8; b++)
			if(ip[i] & (1<<b))
				break;
		mask += b;

		if(b < 8) break;
	}

	return (32 - mask);
}

static uint8_t* get_opt_ip(int key)
{
	struct dhcpopt* opt;

	if(!(opt = get_option(key, 4)))
		return NULL;

	return opt->payload;
}

static int get_opt_int(int key)
{
	struct dhcpopt* opt;

	if(!(opt = get_option(key, 4)))
		return 0;

	return ntohl(*((uint32_t*)opt->payload));
}

void conf_netdev(int ifi, uint8_t* ip, int skipgw)
{
	int mask = maskbits();
	uint8_t* gw = get_opt_ip(DHCP_ROUTER_IP);
	int lt = get_opt_int(DHCP_LEASE_TIME);
	int rt = get_opt_int(DHCP_RENEW_TIME);

	init_netlink();

	set_iface_address(ifi, ip, mask, lt, rt);

	if(!gw || skipgw) return;

	add_default_route(ifi, gw);
}
