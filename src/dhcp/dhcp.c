#include <sys/socket.h>
#include <sys/bind.h>
#include <sys/sendto.h>
#include <sys/recv.h>
#include <sys/ioctl.h>
#include <sys/open.h>
#include <sys/read.h>
#include <sys/close.h>
#include <sys/execve.h>
#include <sys/gettimeofday.h>
#include <bits/socket.h>
#include <bits/packet.h>
#include <bits/ioctl/socket.h>
#include <bits/auxvec.h>

#include <format.h>
#include <string.h>
#include <endian.h>
#include <util.h>
#include <fail.h>

#include "ip.h"
#include "dhcp.h"

#define ARPHRD_NETROM  0
#define ETH_P_IP  0x0800

ERRTAG = "dhcp";
ERRLIST = {
	REPORT(EACCES), REPORT(EPERM), REPORT(EAFNOSUPPORT),
	REPORT(EINVAL), REPORT(ENFILE), REPORT(EMFILE),
	REPORT(ENOBUFS), REPORT(ENOMEM), REPORT(EPROTONOSUPPORT),
	REPORT(EADDRINUSE), REPORT(EBADF), REPORT(ENOTSOCK),
	REPORT(EADDRNOTAVAIL), REPORT(EFAULT), REPORT(ENODEV),
	REPORT(ENETDOWN), RESTASNUMBERS
};

/* DHCP packets are sent via raw sockets, so full ip and udp headers here. */

struct {
	struct iphdr ip;
	struct udphdr udp;
	struct dhcphdr dhcp;
	char options[1200];
} __attribute__((packed)) packet;

int optptr = 0;

/* All communication happens via a single socket, and essentially
   with a single server. The socket is of PF_PACKET variety, bound
   to a netdevice but without any usable ip setup (we assume so). */

int sockfd = 0;

struct {
	int index;
	char name[IFNAMESIZ+1];
	uint8_t mac[6];
} iface;

struct sockaddr_ll sockaddr = {
	.family = AF_PACKET,
	.ifindex = 0, /* to be set */
	.hatype = ARPHRD_NETROM,
	.pkttype = PACKET_HOST,
	.protocol = 0, /* to be set, htons(ETH_P_IP) */
	.halen = 6,
	.addr = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } /* broadcast */
};

struct {
	uint8_t serverip[4];
	uint8_t yourip[4];
} offer;

struct timeval reftv;
struct ifreq ifreq;
char outbuf[1000];

uint32_t xid;

/* Try to come up with a somewhat random xid by pulling auxvec random
   bytes. Failure is not a big issue here, in the sense that DHCP is
   quite insecure by design and a trurly random xid hardly improves that. */

static void init_xid(char** envp)
{
	char** p = envp;

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

/* Socket setup and device queries (mac, ifindex) */

static void ifreq_clear(void)
{
	memset(&ifreq, 0, sizeof(ifreq));
}

static void ifreq_ioctl(int ctl, char* name)
{
	xchk(sysioctl(sockfd, ctl, (long)&ifreq), "ioctl", name);
}

void get_ifindex(char* name)
{
	int nlen = strlen(name);

	if(nlen > IFNAMESIZ)
		fail("name too long:", name, 0);

	ifreq_clear();
	memcpy(ifreq.name, name, nlen);

	ifreq_ioctl(SIOCGIFINDEX, "SIOCGIFINDEX");

	iface.index = ifreq.ival;
	memcpy(iface.name, name, nlen);
	iface.name[nlen] = '\0';
}

void get_ifname(int idx)
{
	ifreq_clear();
	ifreq.ival = idx;

	ifreq_ioctl(SIOCGIFNAME, "SIOCGIFNAME");

	iface.index = idx;
	memcpy(iface.name, ifreq.name, IFNAMESIZ);
	iface.name[IFNAMESIZ] = '\0';
	iface.name[strlen(iface.name)] = '\0';
}

void get_ifhwaddr(void)
{
	ifreq_clear();
	memcpy(ifreq.name, iface.name, IFNAMESIZ);

	ifreq_ioctl(SIOCGIFHWADDR, "SIOCGIFHWADDR");

	if(ifreq.addr.sa_family != 1 /* AF_LOCAL ?!! */)
		fail("unexpected hwaddr family on", iface.name, 0);

	memcpy(iface.mac, ifreq.addr.sa_data, 6);
}

void setup_socket(char* name)
{
	int index = 0;
	char* p;

	sockfd = xchk(syssocket(PF_PACKET, SOCK_DGRAM, 8),
			"socket", "PF_PACKET");

	if((p = parseint(name, &index)) && !*p)
		get_ifname(index);
	else
		get_ifindex(name);

	get_ifhwaddr();

	sockaddr.ifindex = iface.index;
	sockaddr.protocol = htons(ETH_P_IP);

	xchk(sysbind(sockfd, &sockaddr, sizeof(sockaddr)), "bind", NULL);
}

/* Send */

void reset(void)
{
	memset(&packet, 0, sizeof(packet));
	optptr = 0;
}

struct dhcpopt* add_option(int code, int len)
{
	int alloc = len + sizeof(struct dhcpopt);

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

void put_ip(int code, uint8_t* ip)
{
	struct dhcpopt* opt = add_option(code, 4);
	memcpy(opt->payload, ip, 4);
}

void put_mac(int code, uint8_t* mac)
{
	struct dhcpopt* opt = add_option(code, 7);
	opt->payload[0] = 0x01;
	memcpy(opt->payload + 1, mac, 6);
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

	memcpy(packet.dhcp.chaddr, iface.mac, sizeof(iface.mac));

	packet.dhcp.cookie = htonl(DHCP_COOKIE);

	put_byte(DHCP_MESSAGE_TYPE, type);
}

void put_option_end(void)
{
	packet.options[optptr++] = 0xFF;
}

void set_udp_header(void)
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

void send_packet(void)
{
	put_option_end();
	set_udp_header();

	int len = ntohs(packet.ip.tot_len);

	xchk(syssendto(sockfd, &packet, len, 0, &sockaddr, sizeof(sockaddr)),
	     "sendto", NULL);
}

void send_discover(void)
{
	put_header(DHCPDISCOVER);
	send_packet();
}

void send_request(void)
{
	put_header(DHCPREQUEST);
	put_ip(DHCP_REQUESTED_IP, offer.yourip);
	put_ip(DHCP_SERVER_ID, offer.serverip);
	put_mac(DHCP_CLIENT_ID, iface.mac);
	send_packet();
}

/* Receive */

int check_udp_header(void)
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

int check_dhcp_header(void)
{
	if(packet.dhcp.cookie != htonl(DHCP_COOKIE))
		return 1;
	if(packet.dhcp.xid != htonl(xid))
		return 1;

	return 0;
}

void recv_packet(void)
{
	long rd;

	int udplen = sizeof(packet.ip) + sizeof(packet.udp);
	int hdrlen = udplen + sizeof(packet.dhcp);
	int totlen = 0;

	char* buf = (char*) &packet;
	int len = sizeof(packet);

	reset();

	while((rd = sysrecv(sockfd, buf, len, 0)) > 0) {
		if(rd < udplen)
			continue; /* too short */
		if(rd < (totlen = ntohs(packet.ip.tot_len)))
			continue; /* incomplete */
		if(check_udp_header())
			continue; /* malformed, not udp, wrong port */
		if(totlen < hdrlen)
			continue; /* truncated DHCP header */
		if(check_dhcp_header())
			continue; /* malformed DHCP, wrong xid */
		break;
	} if(rd <= 0) {
		fail("recv", NULL, rd);
	}

	optptr = totlen - hdrlen;
}

struct dhcpopt* get_option(int code, int len)
{
	int p = 0;
	struct dhcpopt* opt;

	while(p < optptr - sizeof(*opt)) {
		opt = (struct dhcpopt*)(packet.options + p);
		p += sizeof(*opt) + opt->len;

		if(p > optptr)
			continue; /* truncated option */
		if(opt->code != code)
			continue;
		if(!len || opt->len == len)
			return opt;
		else
			break; /* right code but wrong size */
	}

	return NULL;
}

int get_message_type(void)
{
	struct dhcpopt* opt = get_option(DHCP_MESSAGE_TYPE, 1);
	return opt ? opt->payload[0] : 0;
}

uint8_t* get_server_addr(void)
{
	struct dhcpopt* opt = get_option(DHCP_SERVER_ID, 4);
	return opt ? (uint8_t*)opt->payload : NULL;
}

void recv_offer(void)
{
	uint8_t* srv;

	while(1) {
		recv_packet();

		if(get_message_type() != DHCPOFFER)
			continue;
		else if(!(srv = get_server_addr()))
			continue;
		else
			break;
	}

	memcpy(offer.serverip, srv, 4);
	memcpy(offer.yourip, packet.dhcp.yiaddr, 4);
}

void recv_acknak(void)
{
	while(1) {
		recv_packet();

		if(memcmp(&packet.ip.saddr, offer.serverip, 4))
			continue;

		int mt = get_message_type();

		if(mt == DHCPNAK)
			break;
		if(mt == DHCPACK)
			break;
	}
}

/* Output */

void note_reftime(void)
{
	/* Lease time is relative, but output should be an absolute
	   timestamp. Reference time is DHCPACK reception. */
	xchk(sysgettimeofday(&reftv, NULL), "gettimeofday", NULL);
}

char* fmt_ip(char* p, char* e, uint8_t* ip, int len)
{
	if(len != 4) return p;

	p = fmtint(p, e, ip[0]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[1]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[2]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[3]);

	return p;
}

char* fmt_ips(char* p, char* e, uint8_t* ips, int len)
{
	int i;

	if(!len || len % 4) return p;

	for(i = 0; i < len; i += 4) {
		if(i) p = fmtstr(p, e, " ");
		p = fmt_ip(p, e, ips + i, 4);
	}

	return p;
}

char* fmt_time(char* p, char* e, uint8_t* ptr, int len)
{
	if(len != 4) return p;

	uint32_t val = ntohl(*((uint32_t*)ptr));
	time_t ts = reftv.tv_sec + val;

	p = fmti64(p, e, ts);

	return p;
}

struct showopt {
	int key;
	char* (*fmt)(char*, char*, uint8_t* buf, int len);
	char* tag;
} showopts[] = {
	{  1, fmt_ip,  "subnet" },
	{  3, fmt_ips, "router" },
	{ 54, fmt_ip,  "server" },
	{ 51, fmt_time, "until" },
	{  6, fmt_ips, "dns" },
	{ 42, fmt_ips, "ntp" },
	{  0, NULL, NULL }
};

void show_config(void)
{
	char* p = outbuf;
	char* e = outbuf + sizeof(outbuf);

	p = fmtstr(p, e, "ip ");
	p = fmt_ip(p, e, packet.dhcp.yiaddr, sizeof(packet.dhcp.yiaddr));
	p = fmtstr(p, e, "\n");

	struct showopt* sh;
	struct dhcpopt* opt;

	for(sh = showopts; sh->key; sh++) {
		if(!(opt = get_option(sh->key, 0)))
			continue;
		p = fmtstr(p, e, sh->tag);
		p = fmtstr(p, e, " ");
		p = sh->fmt(p, e, opt->payload, opt->len);
		p = fmtstr(p, e, "\n");
	};

	writeall(STDOUT, outbuf, p - outbuf);
}

#define endof(s) (s + sizeof(s))

static char* arg_ip(char* buf, int size, uint8_t ip[4], int mask)
{
	char* p = buf;
	char* e = buf + size - 1;

	p = fmtip(p, e, ip);

	if(mask > 0 && mask < 32) {
		p = fmtchar(p, e, '/');
		p = fmtint(p, e, mask);
	}

	*p++ = '\0';

	return buf;
}

static int maskbits(void)
{
	struct dhcpopt* opt = get_option(1, 4);
	uint8_t* ip = (uint8_t*)opt->payload;
	int mask = 0;
	int i, b;

	if(!opt) return 0;

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
	struct dhcpopt* opt = get_option(3, 4);
	return opt ? opt->payload : NULL;
}

void exec_ip4cfg(char* devname, char** envp)
{
	char* args[10];
	char** ap = args;
	int ret;

	*ap++ = "ip4cfg";
	*ap++ = devname;

	char ips[30];
	int mask = maskbits();
	*ap++ = arg_ip(ips, sizeof(ips), packet.dhcp.yiaddr, mask);

	char gws[20];
	uint8_t* gw = gateway();

	if(gw) {
		*ap++ = "gw";
		*ap++ = arg_ip(gws, sizeof(gws), gw, 0);
	}

	*ap++ = NULL;

	ret = execvpe(*args, args, envp);
	fail("exec", *args, ret);
}

#define OPTS "nr"
#define OPT_n (1<<0)
#define OPT_r (1<<1)

int main(int argc, char** argv, char** envp)
{
	int i = 1;
	int opts = 0;
	char* devname;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		devname = argv[i++];
	else
		fail("too few arguments", NULL, 0);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	setup_socket(devname);

	init_xid(envp);

	send_discover();
	recv_offer();
	send_request();
	recv_acknak();

	note_reftime();

	if(opts & OPT_n)
		show_config();
	else
		exec_ip4cfg(devname, envp);

	return 0;
}
