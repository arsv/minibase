#include <bits/errno.h>
#include <bits/socket/inet.h>
#include <netlink.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>

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

static void flush_iface(int idx)
{
	struct ifaddrmsg* req;

	nl_header(&nl, req, RTM_DELADDR, 0,
		.family = AF_INET,
		.prefixlen = 0,
		.flags = 0,
		.scope = 0,
		.index = idx);

	long ret = nl_send_recv_ack(&nl);

	if(ret && ret != -EADDRNOTAVAIL)
		fail("RTM_DELADDR", NULL, nl.err);
}

static void set_iface_address(int ifi, uint8_t ip[4], int mask)
{
	struct ifaddrmsg* req;

	nl_header(&nl, req, RTM_NEWADDR, NLM_F_REPLACE,
		.family = AF_INET,
		.prefixlen = mask,
		.flags = 0,
		.scope = 0,
		.index = ifi);
	nl_put(&nl, IFA_LOCAL, ip, 4);

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
		.protocol = RTPROT_STATIC,
		.scope = RT_SCOPE_UNIVERSE,
		.type = RTN_UNICAST,
		.flags = 0);
	nl_put(&nl, RTA_OIF, &oif, sizeof(oif));
	nl_put(&nl, RTA_GATEWAY, gw, 4);

	if(nl_send_recv_ack(&nl))
		fail("RTM_NEWROUTE", NULL, nl.err);
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

static uint8_t* gateway(void)
{
	struct dhcpopt* opt;

	if((opt = get_option(DHCP_ROUTER_IP, 4)))
		return opt->payload;
	else
		return NULL;
}

void conf_netdev(int ifi, uint8_t* ip)
{
	int mask = maskbits();
	uint8_t* gw = gateway();

	init_netlink();

	flush_iface(ifi);
	set_iface_address(ifi, ip, mask);

	if(!gw) return;

	add_default_route(ifi, gw);
}
