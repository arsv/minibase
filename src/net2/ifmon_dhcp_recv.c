#include <bits/arp.h>
#include <bits/socket.h>
#include <bits/socket/packet.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <string.h>
#include <endian.h>
#include <util.h>

#include "ifmon.h"
#include "ifmon_dhcp.h"

struct dhcpopt* opt_at(int off)
{
	struct dhcpopt* opt;
	int hdrlen = sizeof(*opt);

	if(off < 0)
		return NULL;
	if(off > optptr - hdrlen)
		return NULL;

	opt = (struct dhcpopt*)(packet.options + off);

	if(off > optptr - hdrlen - opt->len)
		return NULL; /* truncated opt */

	return opt;
}

struct dhcpopt* first_opt(void)
{
	return opt_at(0);
}

struct dhcpopt* next_opt(struct dhcpopt* curr)
{
	char* cptr = (char*)curr;

	if(cptr < packet.options)
		return NULL;
	if(cptr > packet.options + optptr)
		return NULL;

	int pos = (cptr - packet.options);

	return opt_at(pos + sizeof(*curr) + curr->len);
}

static struct dhcpopt* get_option(int code, int len)
{
	struct dhcpopt* opt;

	for(opt = first_opt(); opt; opt = next_opt(opt))
		if(opt->code != code)
			continue;
		else if(!len || opt->len == len)
			return opt;
		else
			break; /* right code but wrong size */

	return NULL;
}

int get_message_type(void)
{
	struct dhcpopt* opt = get_option(DHCP_MESSAGE_TYPE, 1);
	return opt ? opt->payload[0] : 0;
}

int get_mask_bits(void)
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

uint8_t* get_opt_ip(int key)
{
	struct dhcpopt* opt;

	if(!(opt = get_option(key, 4)))
		return NULL;

	return opt->payload;
}

int get_opt_int(int key)
{
	struct dhcpopt* opt;

	if(!(opt = get_option(key, 4)))
		return 0;

	return ntohl(*((uint32_t*)opt->payload));
}

static int valid_udp_header(void)
{
	int totlen = ntohs(packet.ip.tot_len);
	int udplen = ntohs(packet.udp.len);

	int iphdrlen = sizeof(packet.ip);

	if(packet.ip.protocol != IPPROTO_UDP)
		return 0;
	if(packet.ip.verihl != IPV4IHL5)
		return 0;
	if(packet.udp.dest != htons(BOOT_CLIENT_PORT))
		return 0;
	if(udplen != totlen - iphdrlen)
		return 0;

	return 1;
}

static int valid_dhcp_packet(int rd)
{
	int udplen = sizeof(packet.ip) + sizeof(packet.udp);

	if(rd < udplen)
		return 0; /* too short */

	int totlen = ntohs(packet.ip.tot_len);
	int hdrlen = sizeof(packet) - sizeof(packet.options);

	if(rd < totlen)
		return 0; /* incomplete */
	if(!valid_udp_header())
		return 0; /* malformed, not udp, wrong port */
	if(totlen < hdrlen)
		return 0; /* truncated DHCP header */

	if(packet.dhcp.cookie != htonl(DHCP_COOKIE))
		return 0; /* malformed DHCP packet */

	optptr = totlen - hdrlen;

	return 1;
}

int recv_dhcp_packet(DH, byte mac[6])
{
	struct sockaddr_ll from;
	int frlen = sizeof(from);
	int flags = MSG_DONTWAIT;

	void* buf = &packet;
	int len = sizeof(packet);
	int rd, fd = dh->fd;

	memzero(&packet, sizeof(packet));

	if((rd = sys_recvfrom(fd, buf, len, flags, &from, &frlen)) < 0)
		return rd;
	if(!valid_dhcp_packet(rd))
		return -EINVAL;

	memcpy(mac, from.addr, 6);
	
	return 0;
}
