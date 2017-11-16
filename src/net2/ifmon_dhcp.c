#include <bits/arp.h>
#include <bits/socket.h>
#include <bits/socket/packet.h>
#include <sys/socket.h>
#include <sys/file.h>

#include <printf.h>
#include <string.h>
#include <endian.h>
#include <util.h>

#include "ifmon.h"
#include "ifmon_dhcp.h"

#define ARPHRD_NETROM  0
#define ETH_P_IP  0x0800

static struct dhcpmsg {
	struct iphdr ip;
	struct udphdr udp;
	struct dhcphdr dhcp;
	char options[500];
} __attribute__((packed)) packet;

static int optptr;

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

static void close_socket(DH)
{
	if(dh->fd <= 0)
		return;

	sys_close(dh->fd);
	dh->fd = -1;

	pollset = 0;
}

static void reset_packet(void)
{
	memzero(&packet, sizeof(packet));
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
	reset_packet();

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

static int send_packet(DH)
{
	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = dh->ifi,
		.hatype = ARPHRD_NETROM,
		.pkttype = PACKET_HOST,
		.protocol = htons(ETH_P_IP),
		.halen = 6,
		.addr = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF } /* broadcast */
	};
	int fd = dh->fd;
	int len, ret;

	put_option_end();
	set_udp_header();

	len = ntohs(packet.ip.tot_len);

	if((ret = sys_sendto(fd, &packet, len, 0, &addr, sizeof(addr))) > 0)
		return 0;

	warn("sendto", NULL, ret);

	return ret;
}

static int open_socket(DH)
{
	int fd, ret;
	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = dh->ifi,
		.hatype = ARPHRD_NETROM,
		.pkttype = PACKET_HOST,
		.protocol = htons(ETH_P_IP),
		.halen = 6,
		.addr = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }
	};

	if((fd = sys_socket(PF_PACKET, SOCK_DGRAM, 8)) < 0) {
		warn("socket", NULL, fd);
		return fd;
	}

	if((ret = sys_bind(fd, &addr, sizeof(addr)) < 0)) {
		warn("bind", NULL, fd);
		sys_close(fd);
		return ret;
	}

	dh->fd = fd;

	return 0;
}

static int send_discover(DH)
{
	tracef("%s %i\n", __FUNCTION__, dh->ifi);

	put_header(DHCPDISCOVER, dh);

	return send_packet(dh);
}

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

static int send_request(DH)
{
	tracef("%s %i\n", __FUNCTION__, dh->ifi);

	put_header(DHCPREQUEST, dh);
	put_ip(DHCP_REQUESTED_IP, dh->ouraddr);
	put_ip(DHCP_SERVER_ID, dh->srvaddr);
	put_mac(DHCP_CLIENT_ID, dh->ourmac);

	return send_packet(dh);
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

static int get_message_type(void)
{
	struct dhcpopt* opt = get_option(DHCP_MESSAGE_TYPE, 1);
	return opt ? opt->payload[0] : 0;
}

static void failure(DH, char* msg)
{
	warn("dhcp", msg, 0);
	dhcp_error(dh);
}

static void handle_offer(DH, byte mac[6])
{
	struct dhcpopt* opt;

	tracef("%s %i\n", __FUNCTION__, dh->ifi);

	if(get_message_type() != DHCPOFFER) {
		tracef("not an OFFER message (but %i)\n", get_message_type());
		return;
	}
	if(!(opt = get_option(DHCP_SERVER_ID, 4)))
		return failure(dh, "OFFER without server ip");

	memcpy(dh->srvmac, mac, 6);
	memcpy(dh->srvaddr, opt->payload, 4);
	memcpy(dh->ouraddr, packet.dhcp.yiaddr, 4);

	dh->state = DH_REQUEST;
	dh->tries = 3;

	send_request(dh);
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

static void apply_leased_address(DH)
{
	int mask = maskbits();
	uint8_t* gw = get_opt_ip(DHCP_ROUTER_IP);
	int lt = get_opt_int(DHCP_LEASE_TIME);
	int rt = get_opt_int(DHCP_RENEW_TIME);

	if(rt && rt < lt)
		dh->timer = rt;
	else if(lt)
		dh->timer = lt;
	else
		dh->timer = 0;

	byte* ip = packet.dhcp.yiaddr;
	tracef("leased %i.%i.%i.%i/%i\n", ip[0], ip[1], ip[2], ip[3], mask);
	tracef("lease time %i gw %i.%i.%i.%i\n",
			dh->timer, gw[0], gw[1], gw[2], gw[3]);

	set_iface_address(dh->ifi, ip, mask, lt, rt);

	if(!gw) return;

	add_default_route(dh->ifi, gw);

	report_dhcp_done(dh);
}

static void handle_acknak(DH, byte mac[6])
{
	int type = get_message_type();

	tracef("%s %i\n", __FUNCTION__, dh->ifi);

	if(memcmp(mac, dh->srvmac, 6))
		return; /* packet from another server */
	if(type == DHCPNAK)
		return failure(dh, "got NAK");
	if(type != DHCPACK)
		return;
	if(memcmp(packet.dhcp.yiaddr, dh->ouraddr, 4))
		return failure(dh, "ACK with a different address");

	close_socket(dh);
	apply_leased_address(dh);

	dh->state = DH_LEASED;
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

void handle_dhcp(DH)
{
	struct sockaddr_ll from;
	int frlen = sizeof(from);
	int flags = MSG_DONTWAIT;

	void* buf = &packet;
	int len = sizeof(packet);
	int rd, fd = dh->fd;

	reset_packet();

	if((rd = sys_recvfrom(fd, buf, len, flags, &from, &frlen)) == -EAGAIN)
		return;
	else if(rd < 0)
		return failure(dh, "socket error");

	if(!valid_dhcp_packet(rd))
		return;

	if(dh->state == DH_DISCOVER)
		handle_offer(dh, from.addr);
	else if(dh->state == DH_REQUEST)
		handle_acknak(dh, from.addr);
	else
		tracef("dropping stray packet\n");
}

static void drop_dhcp_slot(DH)
{
	close_socket(dh);
	free_dhcp_slot(dh);
}

void dhcp_error(struct dhcp* dh)
{
	int ifi = dh->ifi;
	struct link* ls;

	tracef("%s %i\n", __FUNCTION__, dh->ifi);

	if((ls = find_link_slot(ifi)))
		ls->flags |= LF_DHCPFAIL;

	drop_dhcp_slot(dh);

	report_dhcp_fail(dh);
}

static void gen_new_xid(DH)
{
	int fd, rd;
	char buf[4];
	char* name = "/dev/urandom";

	if((fd = sys_open(name, O_RDONLY)) < 0)
		return warn(NULL, name, fd);

	if((rd = sys_read(fd, buf, sizeof(buf))) < 0)
		warn("read", name, rd);
	else
		memcpy(&dh->xid, buf, 4);

	sys_close(fd);
}

void start_dhcp(LS)
{
	struct dhcp* dh;
	int ifi = ls->ifi;

	tracef("%s %s=%i\n", __FUNCTION__, ls->name, ls->ifi);

	if(!(dh = grab_dhcp_slot(ifi))) {
		tracef("out of dhcp slots\n");
		ls->flags |= LF_DHCPREQ;
		return;
	} else if(dh->ifi) { /* negotiations in progress */
		if(dh->state != DH_LEASED)
			return;

		tracef("re-dhcp\n");
		memzero(dh, sizeof(*dh));
	}

	dh->ifi = ifi;
	gen_new_xid(dh);
	memcpy(dh->ourmac, ls->mac, 6);

	ls->flags |= LF_DHCPFAIL;

	if(open_socket(dh))
		return;
	if(send_discover(dh))
		return;

	ls->flags &= ~LF_DHCPFAIL;

	dh->state = DH_DISCOVER;
	dh->timer = 500; /* 0.5s to resent DISCOVER */
}

void stop_dhcp(LS)
{
	struct dhcp* dh;

	tracef("%s %s\n", __FUNCTION__, ls->name);

	if(!(dh = find_dhcp_slot(ls->ifi)))
		return;

	drop_dhcp_slot(dh);
}

static void handle_timeout(DH)
{
	tracef("%s %i\n", __FUNCTION__, dh->ifi);
}

void prep_dhcp_timeout(struct timespec* out)
{
	struct dhcp* dh;
	struct timespec ct, ts = { 0, 0 };

	for(dh = dhcps; dh < dhcps + ndhcps; dh++) {
		if(!dh->ifi)
			continue;
		if(!dh->timer)
			continue;

		if(dh->state == DH_LEASED) {
			ct.sec = dh->timer;
			ct.nsec = 0;
		} else {
			ct.sec = dh->timer / 1000;
			ct.nsec = (dh->timer % 1000) * 1000*1000;
		}

		if(!ts.sec && !ts.nsec)
			;
		else if(ts.sec > ct.sec)
			continue;
		else if(ts.nsec > ct.nsec)
			continue;

		ts = ct;
	}

	*out = ts;
}

void update_dhcp_timers(struct timespec* diff)
{
	struct timespec dt = *diff;
	struct dhcp* dh;
	uint sub;

	for(dh = dhcps; dh < dhcps + ndhcps; dh++) {
		if(!dh->ifi)
			continue;
		if(!dh->timer)
			continue;

		if(dh->state == DH_LEASED)
			sub = dt.sec;
		else
			sub = 1000*dt.sec + dt.nsec / 1000*1000;

		if(dh->timer > sub) {
			dh->timer -= sub;
		} else {
			dh->timer = 0;
			handle_timeout(dh);
		}
	}
}
