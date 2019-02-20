#include <bits/arp.h>
#include <bits/socket.h>
#include <bits/socket/inet.h>
#include <bits/socket/packet.h>
#include <bits/auxvec.h>

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/sched.h>
#include <sys/ppoll.h>
#include <sys/signal.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <endian.h>
#include <sigset.h>
#include <util.h>
#include <main.h>

#include "dhcp.h"

/* Socket setup and device queries (mac, ifindex) */

#define ARPHRD_NETROM  0
#define ETH_P_IP  0x0800

int sockfd;
int optptr;
uint32_t xid;

struct sockaddr_ll sockaddr;
struct iface iface;

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

uint16_t ipchecksum(void* addr, int len)
{
	return flipcarry(checksum(addr, len));
}

uint16_t udpchecksum(void* addr, int len, void* ips)
{
	uint32_t sum = checksum(addr, len);
	sum += checksum(ips, 2*4);
	sum += len + IPPROTO_UDP;
	return flipcarry(sum);
}

/* Try to come up with a somewhat random xid by pulling auxvec random
   bytes. Failure is not a big issue here, in the sense that DHCP is
   quite insecure by design and a truly random xid hardly improves that. */

void pick_random_xid(void)
{
	char** p = environ;

	while(*p) p++;

	struct auxvec* a = (struct auxvec*)(p + 1);

	for(; a->key; a++)
		if(a->key == AT_RANDOM)
			break;
	if(a->key)
		memcpy(&xid, (void*)a->val, 4);
	else
		memcpy(&xid, iface.mac + 2, 4); /* meh */
};

void open_raw_socket(void)
{
	int fd, ret;

	if((fd = sys_socket(PF_PACKET, SOCK_DGRAM, 8)) < 0)
		fail("socket", "PF_PACKET", fd);

	sockfd = fd;

	prepare_iface(fd);

	sockaddr = (struct sockaddr_ll) {
		.family = AF_PACKET,
		.ifindex = iface.index,
		.hatype = ARPHRD_NETROM,
		.pkttype = PACKET_HOST,
		.protocol = htons(ETH_P_IP),
		.halen = 6,
		.addr = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } /* broadcast */
	};

	if((ret = sys_bind(fd, &sockaddr, sizeof(sockaddr))) < 0)
		fail("bind", NULL, fd);
}

static void setsockopt(char* tag, int opt, void* val, int len)
{
	int ret, fd = sockfd;
	int level = SOL_SOCKET;

	if((ret = sys_setsockopt(fd, level, opt, val, len)) < 0)
		fail("setsockopt", tag, ret);
}

void open_udp_socket(void)
{
	int fd, ret;

	if((fd = sys_socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0)
		fail("socket", "PF_INET", fd);

	sockfd = fd;

	prepare_iface(fd);

	char buf[IFNAMESIZ+2];
	int nlen = strlen(device);

	if(nlen > IFNAMESIZ)
		fail("too long:", device, 0);

	memzero(buf, sizeof(buf));
	memcpy(buf, device, nlen + 1);
	int one = 1;

	//setsockopt("SO_BROADCAST",    SO_BROADCAST,   &one, sizeof(one));
	setsockopt("SO_REUSEADDR",    SO_REUSEADDR,   &one, sizeof(one));
	setsockopt("SO_BINDTODEVICE", SO_BINDTODEVICE, buf, IFNAMESIZ);

	struct sockaddr_in sa;

	memset(&sa, 0, sizeof(sa));
	sa.family = AF_INET;
	sa.port = htons(BOOT_CLIENT_PORT);
	memcpy(sa.addr, offer.ourip, 4);

	if((ret = sys_bind(fd, &sa, sizeof(sa))) < 0)
		fail("bind", "udp", ret);
}

void close_socket(void)
{
	sys_close(sockfd);
	sockfd = -1;
}

/* Send */

static void reset(void)
{
	memset(&packet, 0, sizeof(packet));
	optptr = 0;
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

void put_byte(int code, uint8_t val)
{
	struct dhcpopt* opt = add_option(code, 1);
	opt->payload[0] = val;
}

void put_mac(int code, byte mac[6])
{
	struct dhcpopt* opt = add_option(code, 7);
	opt->payload[0] = 0x01;
	memcpy(opt->payload + 1, mac, 6);
}

void put_ip(int code, byte ip[4])
{
	struct dhcpopt* opt = add_option(code, 4);
	memcpy(opt->payload, ip, 4);
}

void put_header(int type)
{
	memset(&packet, 0, sizeof(packet));
	optptr = 0;

	packet.dhcp.xid = htonl(xid);
	packet.dhcp.op = BOOTREQUEST;
	packet.dhcp.htype = 1; /* ethernet */
	packet.dhcp.hlen = 6;
	packet.dhcp.hops = 0;

	packet.dhcp.secs = htonl(1);
	packet.dhcp.flags = 0;

	memcpy(packet.dhcp.chaddr, iface.mac, 6);

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

void send_raw_packet(void)
{
	int ret, fd = sockfd;
	void* addr = &sockaddr;
	int alen = sizeof(sockaddr);

	put_option_end();
	set_udp_header();

	int len = ntohs(packet.ip.tot_len);

	if((ret = sys_sendto(fd, &packet, len, 0, addr, alen)) < 0)
		fail("sendto", NULL, ret);
}

void send_udp_packet(void)
{
	int ret, fd = sockfd;

	put_option_end();

	int len = sizeof(packet.dhcp) + optptr;
	void* buf = &packet.dhcp;

	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
	sa.family = AF_INET;
	sa.port = htons(BOOT_SERVER_PORT);
	memcpy(sa.addr, offer.srvip, 4);

	if((ret = sys_sendto(fd, buf, len, 0, &sa, sizeof(sa))) < 0)
		fail("sendto", NULL, ret);
}

/* Receive */

static int check_udp_header(void)
{
	int totlen = ntohs(packet.ip.tot_len);
	int udplen = ntohs(packet.udp.len);

	int iphdrlen = sizeof(packet.ip);

	if(packet.ip.protocol != IPPROTO_UDP)
		return 1;
	if(packet.ip.verihl != IPV4IHL5)
		return 1;
	if(packet.udp.dest != htons(BOOT_CLIENT_PORT))
		return 1;
	if(udplen != totlen - iphdrlen)
		return 1;

	return 0;
}

static int check_dhcp_header(void)
{
	if(packet.dhcp.cookie != htonl(DHCP_COOKIE))
		return 1;
	if(packet.dhcp.xid != htonl(xid))
		return 1;

	return 0;
}

int recv_raw_packet(byte mac[6])
{
	int ret;
	struct timespec ts = { .sec = 1, .nsec = 0 };
	struct pollfd pfd = { .fd = sockfd, .events = POLLIN };
	struct sockaddr_ll from;
	int fromlen = sizeof(from);

	int udplen = sizeof(packet.ip) + sizeof(packet.udp);
	int hdrlen = udplen + sizeof(packet.dhcp);
	int totlen = 0;

	char* buf = (char*) &packet;
	int len = sizeof(packet);
again:
	reset();

	if(!(ret = sys_ppoll(&pfd, 1, &ts, NULL)))
		return -ETIMEDOUT;
	if(ret < 0)
		return ret;

	if((ret = sys_recvfrom(sockfd, buf, len, 0, &from, &fromlen)) < 0)
		return ret;

	if(ret < udplen)
		goto again; /* too short */
	if(ret < (totlen = ntohs(packet.ip.tot_len)))
		goto again; /* incomplete */
	if(check_udp_header())
		goto again; /* malformed, not udp, wrong port */
	if(totlen < hdrlen)
		goto again; /* truncated DHCP header */
	if(check_dhcp_header())
		goto again; /* malformed DHCP, wrong xid */

	optptr = totlen - hdrlen;
	memcpy(mac, from.addr, 6);

	return totlen;
}

int recv_udp_packet(void)
{
	int ret, fd = sockfd;
	struct timespec ts = { .sec = 1, .nsec = 0 };
	struct pollfd pfd = { .fd = sockfd, .events = POLLIN };

	char* buf = (char*) &packet.dhcp;
	int len = sizeof(packet.dhcp) + sizeof(packet.options);
again:
	reset();

	if(!(ret = sys_ppoll(&pfd, 1, &ts, NULL)))
		return -ETIMEDOUT;
	if(ret < 0)
		return ret;

	if((ret = sys_recv(fd, buf, len, 0)) < 0)
		return ret;

	if(ret < (int)sizeof(packet.dhcp))
		goto again; /* too short */
	if(check_dhcp_header())
		goto again; /* malformed DHCP, wrong xid */

	optptr = ret - sizeof(packet.dhcp);

	return ret;
}

static struct dhcpopt* opt_at(int off)
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

struct dhcpopt* get_option(int code, int len)
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

void* get_value(int code, int len)
{
	struct dhcpopt* opt = get_option(code, len);

	return opt ? opt->payload : NULL;
}

int get_opt_int(int code)
{
	int* val = get_value(code, sizeof(int));

	return val ? ntohl(*val) : 0;
}
