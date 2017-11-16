#include <bits/arp.h>
#include <bits/socket.h>
#include <bits/socket/packet.h>
#include <bits/ether.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <string.h>
#include <endian.h>
#include <util.h>

#include "ifmon.h"
#include "ifmon_dhcp.h"

struct dhcpmsg packet;
/* Generic UDP/IP header routines. Does not look like these will be
   used anywhere else any time soon, at least not in this form. */

static uint32_t checksum(void* addr, int len)
{
	uint8_t* buf = addr;
	uint8_t* end = buf + len - 1;
	uint8_t* p = buf;

	uint32_t sum = 0;

	for(p = buf; p < end; p += 2) {
		sum += *(p+0) << 8;
		sum += *(p+1) << 0;
	} if(len & 1) {
		sum += *(p+0) << 8;
	}

	return sum;
}

static uint16_t flipcarry(uint32_t sum)
{
	sum = (sum & 0xFFFF) + (sum >> 16);
	sum = (sum & 0xFFFF) + (sum >> 16);
	return (~sum & 0xFFFF);
}

static uint16_t ipchecksum(void* addr, int len)
{
	return flipcarry(checksum(addr, len));
}

static uint16_t udpchecksum(void* addr, int len, void* ips)
{
	uint32_t sum = checksum(addr, len);
	sum += checksum(ips, 2*4);
	sum += len + IPPROTO_UDP;
	return flipcarry(sum);
}

static struct dhcpopt* add_option(int code, int len)
{
	uint alloc = len + sizeof(struct dhcpopt);

	if(optptr + alloc >= sizeof(packet.options) - 1)
		fail("out of packet space", NULL, 0);

	struct dhcpopt* opt = (struct dhcpopt*)(packet.options + optptr);

	opt->code = code;
	opt->len = len;

	optptr += alloc;

	return opt;
}

static void put_byte(int code, uint8_t val)
{
	struct dhcpopt* opt = add_option(code, 1);
	opt->payload[0] = val;
}

static void put_ip(int code, uint8_t* ip)
{
	struct dhcpopt* opt = add_option(code, 4);
	memcpy(opt->payload, ip, 4);
}

static void put_mac(int code, uint8_t* mac)
{
	struct dhcpopt* opt = add_option(code, 7);
	opt->payload[0] = 0x01;
	memcpy(opt->payload + 1, mac, 6);
}

static void put_header(int type, DH)
{
	memzero(&packet, sizeof(packet));
	optptr = 0;

	packet.dhcp.xid = htonl(dh->xid);
	packet.dhcp.op = BOOTREQUEST;
	packet.dhcp.htype = 1; /* ethernet */
	packet.dhcp.hlen = 6;
	packet.dhcp.hops = 0;

	packet.dhcp.secs = htonl(1);
	packet.dhcp.flags = 0;

	memcpy(packet.dhcp.chaddr, dh->ourmac, sizeof(dh->ourmac));

	packet.dhcp.cookie = htonl(DHCP_COOKIE);

	put_byte(DHCP_MESSAGE_TYPE, type);
}

static void put_option_end(void)
{
	packet.options[optptr++] = 0xFF;
}

static void set_udp_header(void)
{
	int udpsize = sizeof(packet.udp) + sizeof(packet.dhcp) + optptr;
	int ipsize = udpsize + sizeof(packet.ip);

	packet.ip.protocol = IPPROTO_UDP;
	memset(packet.ip.saddr, 0x00, 4);
	memset(packet.ip.daddr, 0xFF, 4);
	void* ips = &packet.ip.saddr;

	packet.udp.source = htons(BOOT_CLIENT_PORT);
	packet.udp.dest = htons(BOOT_SERVER_PORT);
	packet.udp.len = htons(udpsize);
	packet.udp.check = htons(udpchecksum(&packet.udp, udpsize, ips));

	packet.ip.tot_len = htons(ipsize);
	packet.ip.verihl = IPV4IHL5;
	packet.ip.ttl = IPDEFTTL;
	packet.ip.check = htons(ipchecksum(&packet, sizeof(packet.ip)));
}

static int send_packet(DH, const byte mac[6])
{
	struct sockaddr_ll to = {
		.family = AF_PACKET,
		.ifindex = dh->ifi,
		.hatype = 0,
		.pkttype = PACKET_HOST,
		.protocol = htons(ETH_P_IP),
		.halen = 6
	};
	int fd = dh->fd;
	int len, ret;

	memcpy(to.addr, mac, 6);

	put_option_end();
	set_udp_header();

	len = ntohs(packet.ip.tot_len);

	if((ret = sys_sendto(fd, &packet, len, 0, &to, sizeof(to))) > 0)
		return 0;

	warn("sendto", NULL, ret);

	return ret;
}

static int send_broadcast(DH)
{
	static const byte bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

	return send_packet(dh, bcast);
}

static int send_unicast(DH)
{
	return send_packet(dh, dh->srvmac);
}

int send_discover(DH)
{
	put_header(DHCPDISCOVER, dh);

	return send_broadcast(dh);
}

int send_request(DH)
{
	put_header(DHCPREQUEST, dh);
	put_ip(DHCP_REQUESTED_IP, dh->ouraddr);
	put_ip(DHCP_SERVER_ID, dh->srvaddr);
	put_mac(DHCP_CLIENT_ID, dh->ourmac);

	return send_broadcast(dh);
}

int send_renew(DH)
{
	put_header(DHCPREQUEST, dh);
	memcpy(packet.dhcp.ciaddr, dh->ouraddr, 4);

	return send_unicast(dh);
}
