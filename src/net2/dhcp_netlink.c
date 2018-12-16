#include <bits/errno.h>
#include <bits/socket/inet.h>
#include <netlink.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/route.h>
#include <bits/ioctl/socket.h>
#include <sys/ioctl.h>

#include <string.h>
#include <endian.h>
#include <util.h>

#include "dhcp.h"

char rxbuf[512];
char txbuf[512];

struct netlink nl;

static void get_ifindex(int fd, struct ifreq* ifreq)
{
	int ret;

	if((ret = sys_ioctl(fd, SIOCGIFINDEX, ifreq)) < 0)
		fail("ioctl", "SIOCGIFINDEX", ret);

	iface.index = ifreq->ival;
}

static void get_ifhwaddr(int fd, struct ifreq* ifreq)
{
	int ret;

	if((ret = sys_ioctl(fd, SIOCGIFHWADDR, ifreq)) < 0)
		fail("ioctl", "SIOCGIFHWADDR", ret);
	if(ifreq->addr.family != ARPHRD_ETHER)
		fail("unexpected hwaddr family on", device, 0);

	memcpy(iface.mac, ifreq->addr.data, 6);
}

void prepare_iface(int fd)
{
	struct ifreq ifreq;

	if(iface.index)
		return;

	memzero(&ifreq, sizeof(ifreq));

	int nlen = strlen(device);

	if(nlen > (int)sizeof(ifreq.name))
		fail("too long:", device, 0);
	if(nlen == sizeof(ifreq.name))
		memcpy(ifreq.name, device, nlen);
	else
		memcpy(ifreq.name, device, nlen + 1);

	get_ifindex(fd, &ifreq);
	get_ifhwaddr(fd, &ifreq);
}

static void init_netlink(void)
{
	int ret;

	if(nl.fd)
		return;

	nl_init(&nl);
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));

	if((ret = nl_connect(&nl, NETLINK_ROUTE, 0)) < 0)
		fail("connect", "NETLINK_ROUTE", ret);

	prepare_iface(nl.fd);
}

static uint8_t* get_opt_ip(int key)
{
	struct dhcpopt* opt;

	if(!(opt = get_option(key, 4)))
		return NULL;

	return opt->payload;
}

static int maskbits(void)
{
	struct dhcpopt* opt;
	int mask = 0;
	int i, b;

	if(!(opt = get_option(DHCP_NETMASK, 4)))
		return 32;

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

static void rtm_newaddr_msg(int flags)
{
	struct ifaddrmsg* req;

	byte* ip = offer.ourip;
	int mask = maskbits();
	int lt = get_opt_int(DHCP_LEASE_TIME);
	int rt = get_opt_int(DHCP_RENEW_TIME);

	nl_header(&nl, req, RTM_NEWADDR, flags,
		.family = AF_INET,
		.prefixlen = mask,
		.flags = 0,
		.scope = 0,
		.index = iface.index);
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

	int ret;

	if((ret = nl_send_recv_ack(&nl)) < 0)
		fail("RTM_NEWADDR", NULL, ret);
}

static void set_iface_address(void)
{
	rtm_newaddr_msg(NLM_F_CREATE | NLM_F_EXCL);
}

static void add_default_route(void)
{
	struct rtmsg* req;
	uint32_t oif = iface.index;
	uint8_t* gw;
	
	if(!(gw = get_opt_ip(DHCP_ROUTER_IP)))
		return;

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

static void clear_iface_addrs(void)
{
	struct ifaddrmsg* req;
	int ret, tries = 20;
again:	
	nl_header(&nl, req, RTM_DELADDR, NLM_F_ACK,
		.family = AF_INET,
		.prefixlen = 0,
		.flags = 0,
		.scope = 0,
		.index = iface.index);
	
	ret = nl_send_recv_ack(&nl);

	if(ret == 0 && --tries > 0)
		goto again;
	if(ret != -EADDRNOTAVAIL)
		warn("RTM_DELADDR", NULL, ret);
}

void flush_iface(void)
{
	init_netlink();
	clear_iface_addrs();
}

void configure_iface(void)
{
	init_netlink();
	clear_iface_addrs();
	set_iface_address();
	add_default_route();
}

void update_lifetime(void)
{
	init_netlink();
	rtm_newaddr_msg(NLM_F_REPLACE);
}
