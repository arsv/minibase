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

#define RETRIES 2 /* plus the initial packet */
#define TIMEOUT 500 /* ms */

struct dhcpmsg packet;
int optptr;

static int open_socket(DH)
{
	int fd, ret;
	struct sockaddr_ll addr = {
		.family = AF_PACKET,
		.ifindex = dh->ifi,
		.hatype = 0,
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
	pollset = 0;

	return 0;
}

static void close_socket(DH)
{
	if(dh->fd <= 0)
		return;

	sys_close(dh->fd);
	dh->fd = -1;

	pollset = 0;
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

static void failure(DH, char* msg)
{
	warn("dhcp", msg, 0);
	dhcp_error(dh);
}

static int next_renew_delay(int timeleft)
{
	if(timeleft > 2*60)
		return timeleft / 2;
	else
		return timeleft;
}

static void renew_failed(DH)
{
	struct link* ls;

	close_socket(dh);

	if(dh->extra) { /* some more time left */
		int delay = next_renew_delay(dh->extra);
		dh->timer = delay;
		dh->extra -= delay;
		dh->state = DH_LEASED;
	} else { /* no more time left */
		if((ls = find_link_slot(dh->ifi)))
			stop_link(ls);
		free_dhcp_slot(dh);
	}
}

static void handle_offer(DH, byte mac[6])
{
	byte* ip;

	if(get_message_type() != DHCPOFFER)
		return;
	if(!(ip = get_opt_ip(DHCP_SERVER_ID)))
		return failure(dh, "OFFER without server ip");

	memcpy(dh->srvmac, mac, 6);
	memcpy(dh->srvaddr, ip, 4);
	memcpy(dh->ouraddr, packet.dhcp.yiaddr, 4);

	if(send_request(dh))
		return;

	dh->state = DH_REQUEST;
	dh->tries = RETRIES;
}

static void apply_leased_address(DH)
{
	int mask = get_mask_bits();
	int lt = get_opt_int(DHCP_LEASE_TIME);
	int rt = get_opt_int(DHCP_RENEW_TIME);
	byte *gw, *ip = packet.dhcp.yiaddr;

	if(!rt && lt > 2*60)
		rt = lt/2;
	if(rt && rt < lt)
		dh->timer = rt;
	else if(lt)
		dh->timer = lt;
	else
		dh->timer = 0;

	dh->extra = lt - dh->timer;

	set_iface_address(dh->ifi, ip, mask, lt, rt);

	if(dh->state == DH_RENEWING)
		return;
	if(!(gw = get_opt_ip(DHCP_ROUTER_IP)))
		return;

	add_default_route(dh->ifi, gw);
}

static void handle_acknak(DH, byte mac[6])
{
	int type = get_message_type();

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

	report_dhcp_done(dh);
}

static void handle_rebind(DH, byte mac[6])
{
	int type = get_message_type();

	if(memcmp(mac, dh->srvmac, 6))
		return; /* packet from another server */
	if(type == DHCPNAK)
		return renew_failed(dh);
	if(type != DHCPACK)
		return;
	if(memcmp(packet.dhcp.yiaddr, dh->ouraddr, 4))
		return renew_failed(dh);

	close_socket(dh);
	apply_leased_address(dh);

	dh->state = DH_LEASED;
}

static void renew_lease(DH)
{
	open_socket(dh);

	if(send_renew(dh))
		return renew_failed(dh);

	dh->state = DH_RENEWING;
	dh->tries = RETRIES;
	dh->timer = TIMEOUT;
}

static void handle_timeout(DH)
{
	int ret;

	if(dh->state == DH_LEASED)
		return renew_lease(dh);

	if(dh->tries)
		dh->tries--;
	else if(dh->state == DH_RENEWING)
		return renew_failed(dh);
	else
		return failure(dh, "timeout");

	if(dh->state == DH_DISCOVER)
		ret = send_discover(dh);
	else if(dh->state == DH_REQUEST)
		ret = send_request(dh);
	else if(dh->state == DH_RENEWING)
		ret = send_renew(dh);
	else return;

	if(!ret)
		dh->timer = TIMEOUT;
	else
		dhcp_error(dh);
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
			sub = 1000*dt.sec + dt.nsec/1000/1000;

		if(dh->timer > sub) {
			dh->timer -= sub;
		} else {
			dh->timer = 0;
			handle_timeout(dh);
		}
	}
}

void dhcp_error(struct dhcp* dh)
{
	int ifi = dh->ifi;
	struct link* ls;

	if((ls = find_link_slot(ifi)))
		ls->flags |= LF_DHCPFAIL;

	close_socket(dh);
	free_dhcp_slot(dh);

	report_dhcp_fail(dh);
}

void stop_dhcp(LS)
{
	struct dhcp* dh;

	if(!(dh = find_dhcp_slot(ls->ifi)))
		return;

	close_socket(dh);
	free_dhcp_slot(dh);
}

void handle_dhcp(DH)
{
	byte mac[6];

	if(recv_dhcp_packet(dh, mac) < 0)
		return;

	if(dh->state == DH_DISCOVER)
		handle_offer(dh, mac);
	else if(dh->state == DH_REQUEST)
		handle_acknak(dh, mac);
	else if(dh->state == DH_RENEWING)
		handle_rebind(dh, mac);
}

void start_dhcp(LS)
{
	struct dhcp* dh;
	int ifi = ls->ifi;

	if(!(dh = grab_dhcp_slot(ifi))) {
		ls->flags |= LF_DHCPREQ;
		return;
	} else if(dh->ifi) { /* negotiations in progress */
		if(dh->state != DH_LEASED)
			return;
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
	dh->tries = RETRIES;
	dh->timer = TIMEOUT;
}
