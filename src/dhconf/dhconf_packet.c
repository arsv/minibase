#include <bits/socket/packet.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <endian.h>
#include <string.h>
#include <util.h>

#include "dhconf.h"

#define CMDSIZE 350
#define REPSIZE 750

static const byte bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

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

static void put_ip_udp(struct dhcpmsg* msg, CTX, DH)
{
	struct iphdr* ip = &msg->ip;
	struct udphdr* udp = &msg->udp;

	int ipsize = dh->ptr;
	int udpsize = ipsize - sizeof(msg->ip);

	ip->protocol = IPPROTO_UDP;
	memset(ip->saddr, 0x00, 4);
	memset(ip->daddr, 0xFF, 4);
	void* ips = ip->saddr;

	udp->source = htons(BOOT_CLIENT_PORT);
	udp->dest = htons(BOOT_SERVER_PORT);
	udp->len = htons(udpsize);
	udp->check = htons(udpchecksum(udp, udpsize, ips));

	ip->tot_len = htons(ipsize);
	ip->verihl = IPV4IHL5;
	ip->ttl = IPDEFTTL;
	ip->check = htons(ipchecksum(ip, sizeof(*ip)));
}

static void* alloc(DH, uint len)
{
	uint ptr = dh->ptr;
	uint max = dh->max;
	void* buf = dh->buf;

	if(ptr + len > max)
		fail("out of packet space", NULL, 0);

	dh->ptr = ptr + len;

	return buf + ptr;
}

static struct dhcpopt* add_option(DH, int code, int len)
{
	uint need = sizeof(struct dhcpopt) + len;
	struct dhcpopt* opt = alloc(dh, need);

	opt->code = code;
	opt->len = len;

	return opt;
}

static void add_byte(DH, int code, uint8_t val)
{
	struct dhcpopt* opt = add_option(dh, code, 1);
	opt->payload[0] = val;
}

static void add_mac(DH, int code, byte mac[6])
{
	struct dhcpopt* opt = add_option(dh, code, 7);
	opt->payload[0] = 0x01;
	memcpy(opt->payload + 1, mac, 6);
}

static void add_ip(DH, int code, byte ip[4])
{
	struct dhcpopt* opt = add_option(dh, code, 4);
	memcpy(opt->payload, ip, 4);
}

static void add_raw(DH, int code, void* data, int len)
{
	struct dhcpopt* opt = add_option(dh, code, len);
	memcpy(opt->payload, data, len);
}

static void put_header(struct dhcpmsg* msg, CTX)
{
	struct dhcphdr* dhcp = &msg->dhcp;

	dhcp->xid = htonl(ctx->xid);
	dhcp->op = BOOTREQUEST;
	dhcp->htype = 1; /* ethernet */
	dhcp->hlen = 6;
	dhcp->hops = 0;

	dhcp->secs = htonl(1);
	dhcp->flags = 0;

	memcpy(dhcp->chaddr, ctx->ourmac, 6);

	dhcp->cookie = htonl(DHCP_COOKIE);
}

static void add_optend(DH)
{
	byte* end = alloc(dh, 1);
	*end = 0xFF;
}

static void send_raw_packet(CTX, DH, const byte dst[6])
{
	int fd = reopen_raw_socket(ctx);
	int ret;
	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = ctx->ifindex,
		.hatype = ARPHRD_NETROM,
		.pkttype = PACKET_HOST,
		.protocol = htons(ETH_P_IP),
		.halen = 6,
	};
	int alen = sizeof(addr);

	memcpy(addr.addr, dst, 6);

	void* buf = dh->buf;
	uint len = dh->ptr;

	if((ret = sys_sendto(fd, buf, len, 0, &addr, alen)) < 0)
		quit(ctx, "send", ret);
}

static void send_discover(CTX)
{
	byte buf[CMDSIZE];
	struct dhcmd dh = { buf, 0, sizeof(buf) };
	struct dhcpmsg* msg = alloc(&dh, sizeof(*msg));

	memzero(msg, sizeof(*msg));
	put_header(msg, ctx);
	add_byte(&dh, DHCP_MESSAGE_TYPE, DHCPDISCOVER);
	add_optend(&dh);
	put_ip_udp(msg, ctx, &dh);

	send_raw_packet(ctx, &dh, bcast);

	set_timer(ctx, 1);
}

static void send_request(CTX)
{
	byte buf[CMDSIZE];
	struct dhcmd dh = { buf, 0, sizeof(buf) };
	struct dhcpmsg* msg = alloc(&dh, sizeof(*msg));
	byte params[] = { 1, 3, 6, 42 };

	memzero(msg, sizeof(*msg));
	put_header(msg, ctx);
	add_byte(&dh, DHCP_MESSAGE_TYPE, DHCPREQUEST);
	add_ip(&dh, DHCP_REQUESTED_IP, ctx->ourip);
	add_ip(&dh, DHCP_SERVER_ID, ctx->srvip);
	add_mac(&dh, DHCP_CLIENT_ID, ctx->ourmac);
	add_raw(&dh, DHCP_PARAM_REQ, params, sizeof(params));
	add_optend(&dh);
	put_ip_udp(msg, ctx, &dh);

	send_raw_packet(ctx, &dh, bcast);

	set_timer(ctx, 1);
}

static void send_renew(CTX)
{
	byte buf[CMDSIZE];
	struct dhcmd dh = { buf, 0, sizeof(buf) };
	struct dhcpmsg* msg = alloc(&dh, sizeof(*msg));

	memzero(msg, sizeof(*msg));
	put_header(msg, ctx);
	memcpy(msg->dhcp.ciaddr, ctx->ourip, 4);
	add_byte(&dh, DHCP_MESSAGE_TYPE, DHCPREQUEST);
	add_ip(&dh, DHCP_REQUESTED_IP, ctx->ourip);
	add_mac(&dh, DHCP_CLIENT_ID, ctx->ourmac);
	add_optend(&dh);
	put_ip_udp(msg, ctx, &dh);

	send_raw_packet(ctx, &dh, bcast);

	set_timer(ctx, 1);
}

static void send_release(CTX)
{
	byte buf[CMDSIZE];
	struct dhcmd dh = { buf, 0, sizeof(buf) };
	struct dhcpmsg* msg = alloc(&dh, sizeof(*msg));

	memzero(msg, sizeof(*msg));
	put_header(msg, ctx);
	add_byte(&dh, DHCP_MESSAGE_TYPE, DHCPRELEASE);
	add_ip(&dh, DHCP_SERVER_ID, ctx->srvip);
	add_mac(&dh, DHCP_CLIENT_ID, ctx->ourmac);
	add_optend(&dh);
	put_ip_udp(msg, ctx, &dh);

	send_raw_packet(ctx, &dh, bcast);
}

static uint64_t current_time(CTX)
{
	struct timespec ts;
	int ret;

	if((ret = sys_clock_gettime(CLOCK_BOOTTIME, &ts)) < 0)
		quit(ctx, "clock_gettime", ret);

	return ts.sec;
}

static void store_timings(CTX, MSG, int optlen)
{
	uint* opt;

	if(!(opt = get_msg_opt(msg, optlen, DHCP_LEASE_TIME, 4)))
		quit(ctx, "no lease time option", 0);

	uint lt = ntohl(*opt);

	ctx->lease_time = lt;
	ctx->renew_time = lt/2;
}

static void store_options(CTX, MSG, int optlen)
{
	int maxlen = sizeof(ctx->options);

	if(optlen > maxlen)
		optlen = maxlen;

	ctx->optlen = optlen;
	memcpy(ctx->options, msg->options, optlen);
}

static void check_offer(CTX, MSG, int optlen, byte mac[6])
{
	byte* type;
	byte* srvid;

	if(!(type = get_msg_opt(msg, optlen, DHCP_MESSAGE_TYPE, 1)))
		return;
	if(*type != DHCPOFFER)
		return;
	if(!(srvid = get_msg_opt(msg, optlen, DHCP_SERVER_ID, 4)))
		quit(ctx, "offer without srvid", 0);

	memcpy(ctx->srvip, srvid, 4);
	memcpy(ctx->ourip, msg->dhcp.yiaddr, 4);
	memcpy(ctx->srvmac, mac, 6);

	ctx->timeref = current_time(ctx);

	set_state(ctx, ST_REQUEST);
}

static void check_acknak(CTX, MSG, int optlen, byte mac[6])
{
	struct iphdr* ip = &msg->ip;

	if(memcmp(ctx->srvmac, mac, 6))
		return; /* somebody else replied */
	if(memcmp(ctx->srvip, ip->saddr, 4))
		return; /* not the server we want */

	byte* tp;

	if(!(tp = get_msg_opt(msg, optlen, DHCP_MESSAGE_TYPE, 1)))
		return; /* invalid message */
	if(*tp == DHCPNAK)
		quit(ctx, "lease NAK", 0);
	if(*tp != DHCPACK)
		return;

	set_state(ctx, ST_LEASED);

	memcpy(ctx->ourip, msg->dhcp.yiaddr, 4);

	store_options(ctx, msg, optlen);
	store_timings(ctx, msg, optlen);

	ctx->origref = ctx->timeref;
}

static void check_newack(CTX, MSG, int optlen, byte mac[6])
{
	struct iphdr* ip = &msg->ip;

	if(memcmp(ctx->srvmac, mac, 6))
		return; /* somebody else replied */
	if(memcmp(ctx->srvip, ip->saddr, 4))
		return; /* same, not the server we want */

	byte* tp;

	if(!(tp = get_msg_opt(msg, optlen, DHCP_MESSAGE_TYPE, 1)))
		return; /* invalid message */

	int type = *tp;

	if(type == DHCPACK)
		set_state(ctx, ST_LEASED);
	else if(type == DHCPNAK)
		set_state(ctx, ST_DECONF);
}

static void handle_packet(CTX, struct dhcpmsg* msg, int optlen, byte mac[6])
{
	struct iphdr* ip = &msg->ip;
	struct udphdr* udp = &msg->udp;
	struct dhcphdr* dhcp = &msg->dhcp;

	int totlen = ntohs(ip->tot_len);
	int udplen = ntohs(udp->len);
	int iphdrlen = sizeof(*ip);

	if(ip->protocol != IPPROTO_UDP)
		return;
	if(ip->verihl != IPV4IHL5)
		return;
	if(udp->dest != htons(BOOT_CLIENT_PORT))
		return;

	if(udplen != totlen - iphdrlen)
		return;

	if(dhcp->cookie != htonl(DHCP_COOKIE))
		return;
	if(dhcp->xid != htonl(ctx->xid))
		return;

	int state = ctx->state;

	if(state == ST_DISCOVER)
		check_offer(ctx, msg, optlen, mac);
	else if(state == ST_REQUEST)
		check_acknak(ctx, msg, optlen, mac);
	else if(state == ST_RENEWING)
		check_newack(ctx, msg, optlen, mac);
}

static void recv_packet(CTX)
{
	byte buf[REPSIZE];
	struct sockaddr_ll from;
	int fromlen = sizeof(from);
	int len = sizeof(buf);
	int fd = ctx->rawfd;
	int ret;

	if((ret = sys_recvfrom(fd, buf, len, 0, &from, &fromlen)) == 0)
		quit(ctx, "EOF in raw socket", 0);
	else if(ret == -EAGAIN)
		return;
	else if(ret < 0)
		quit(ctx, "recv", ret);

	struct dhcpmsg* msg = (void*)buf;

	if(ret < ssizeof(*msg))
		return;

	int optlen = ret - sizeof(*msg);

	handle_packet(ctx, msg, optlen, from.addr);
}

static void retry_discover(CTX)
{
	deconf_iface(ctx);

	set_state(ctx, ST_DISCOVER);

	send_discover(ctx);
}

static void lease_obtained(CTX)
{
	set_timer(ctx, ctx->renew_time);

	config_iface(ctx);

	output_lease_info(ctx);
	proceed_with_scripts(ctx);
}

static void lease_renewed(CTX)
{
	set_timer(ctx, ctx->renew_time);

	update_iface(ctx);
}

static void lease_lost(CTX)
{
	retry_discover(ctx);
}

static void time_to_renew(CTX)
{
	uint64_t now = current_time(ctx);
	uint64_t ref = ctx->timeref;
	uint lease_time = ctx->lease_time;

	if(now >= ref + lease_time) {
		retry_discover(ctx);
	} else {
		set_state(ctx, ST_RENEWING);
		send_renew(ctx);
	}
}

static void renew_timeout(CTX)
{
	uint lease_time = ctx->lease_time;
	uint64_t now = current_time(ctx);
	uint64_t ref = ctx->timeref;
	uint64_t end = ref + lease_time - 10*60;

	if(now > end) /* less than 10 minutes left */
		return retry_discover(ctx);

	ctx->state = ST_LEASED;
	set_timer(ctx, 60);
}

void timeout_waiting(CTX)
{
	int state = ctx->state;
	int count = ctx->count;

	ctx->count = count + 1;

	if(state == ST_LEASED)
		return time_to_renew(ctx);
	if(state == ST_RENEWING)
		return renew_timeout(ctx);

	if(state == ST_DISCOVER && count >= 5)
		quit(ctx, NULL, -ETIMEDOUT);
	else if(count >= 3)
		quit(ctx, NULL, -ETIMEDOUT);

	if(state == ST_DISCOVER)
		return send_discover(ctx);
	if(state == ST_REQUEST)
		return send_request(ctx);

	quit(ctx, "timed out, state", state);
}

static void next_action(CTX, int state0, int state1)
{
	if(state1 == ST_DISCOVER)
		return send_discover(ctx);
	if(state1 == ST_REQUEST)
		return send_request(ctx);
	if(state1 == ST_LEASED && state0 == ST_REQUEST)
		return lease_obtained(ctx);
	if(state1 == ST_LEASED && state0 == ST_RENEWING)
		return lease_renewed(ctx);
	if(state1 == ST_DECONF)
		return lease_lost(ctx);

	quit(ctx, "invalid next, state", state1);
}

void recv_incoming(CTX)
{
	int state0 = ctx->state;

	recv_packet(ctx);

	int state1 = ctx->state;

	if(state1 == state0) return;

	next_action(ctx, state0, state1);
}

void start_discover(CTX)
{
	set_state(ctx, ST_DISCOVER);

	send_discover(ctx);
}

void release_address(CTX)
{
	int state = ctx->state;

	if(state == ST_LEASED)
		;
	else if(state == ST_RENEWING)
		;
	else return;

	send_release(ctx);

	set_state(ctx, ST_RELEASED);
}
