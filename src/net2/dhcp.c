#include <bits/arp.h>
#include <bits/socket.h>
#include <bits/socket/packet.h>
#include <bits/ioctl/socket.h>
#include <bits/auxvec.h>

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

ERRTAG("dhcp");

struct dhcpmsg packet;

int optptr;
int sockfd;

char* device;
char** environ;
struct ifreq ifreq;
struct offer offer;
uint32_t xid;

static void sigint(int sig)
{
	(void)sig;
	warn("SIGINT", NULL, 0);
	/* reset signal action; the next SIGINT should kill the process */
	SIGHANDLER(sa, SIG_DFL, 0);
	sys_sigaction(SIGINT, &sa, NULL);
}

static void setup_release_on_sigint(void)
{
	SIGHANDLER(sa, sigint, 0);
	sys_sigaction(SIGINT, &sa, NULL);
}

static int get_message_type(void)
{
	struct dhcpopt* opt = get_option(DHCP_MESSAGE_TYPE, 1);
	return opt ? opt->payload[0] : 0;
}

static uint8_t* get_server_addr(void)
{
	struct dhcpopt* opt = get_option(DHCP_SERVER_ID, 4);
	return opt ? (uint8_t*)opt->payload : NULL;
}

static void send_discover_recv_offer(void)
{
	uint8_t* srv;
	byte mac[6];
	int ret, tries = 4;
resend:
	put_header(DHCPDISCOVER);
	send_raw_packet();
rerecv:
	if((ret = recv_raw_packet(mac)) >= 0)
		;
	else if(ret == -ETIMEDOUT && --tries)
		goto resend;
	else
		fail(NULL, "discover", ret);

	if(get_message_type() != DHCPOFFER)
		goto rerecv; /* some stray DHCP packet */
	if(!(srv = get_server_addr()))
		fail("offer without srvid", NULL, 0);

	memcpy(offer.srvip, srv, 4);
	memcpy(offer.ourip, packet.dhcp.yiaddr, 4);
	memcpy(offer.ourmac, iface.mac, 6);
	memcpy(offer.srvmac, mac, 6);
}

static void send_request_recv_acknak(void)
{
	int ret, tries = 2;
	byte mac[6];
resend:
	put_header(DHCPREQUEST);
	put_ip(DHCP_REQUESTED_IP, offer.ourip);
	put_ip(DHCP_SERVER_ID, offer.srvip);
	put_mac(DHCP_CLIENT_ID, offer.ourmac);
	send_raw_packet();
rerecv:
	if((ret = recv_raw_packet(mac)) >= 0)
		;
	else if(ret == -ETIMEDOUT && --tries > 0)
		goto resend;
	else
		fail(NULL, "request", ret);

	if(memcmp(mac, offer.srvmac, 6))
		goto rerecv; /* somebody else replied */
	if(memcmp(&packet.ip.saddr, offer.srvip, 4))
		goto rerecv; /* same, not the server we want */

	ret = get_message_type();

	if(ret == DHCPNAK)
		fail(NULL, "got NAK", 0);
	if(ret != DHCPACK)
		goto rerecv;
}

static int send_renew_recv_acknak(void)
{
	int ret, tries = 2;
resend:
	put_header(DHCPREQUEST);
	memcpy(packet.dhcp.ciaddr, offer.ourip, 4);
	send_udp_packet();
rerecv:
	if((ret = recv_udp_packet()) >= 0)
		;
	else if(ret == -ETIMEDOUT && --tries > 0)
		goto resend;
	else
		return ret;

	ret = get_message_type();

	if(ret == DHCPNAK) {
		warn(NULL, "server refused to renew the lease", 0);
		return -ENOMSG;
	} else if(ret != DHCPACK) {
		goto rerecv;
	}

	return 0;

}

static void send_release(void)
{
	put_header(DHCPRELEASE);
	put_ip(DHCP_SERVER_ID, offer.srvip);
	put_mac(DHCP_CLIENT_ID, offer.ourmac);
	send_raw_packet();
}

static int sleep_lease_time(void)
{
	int lt = get_opt_int(DHCP_LEASE_TIME);
	int rt = get_opt_int(DHCP_RENEW_TIME);

	if(!lt)
		return sys_pause();

	int time = (rt && rt < lt) ? rt : (lt/2);

	struct timespec ts = {
		.sec = time,
		.nsec = 0
	};

	int ret;

	if((ret = sys_nanosleep(&ts, NULL)) >= 0)
		;
	else if(ret != -EINTR)
		warn("sleep", NULL, ret);

	return ret;
}

static int lease_wait()
{
	int ret = 0;

	check_no_lease();

	open_raw_socket();
	pick_random_xid();
	send_discover_recv_offer();
	send_request_recv_acknak();
	close_socket();

	show_config();
	configure_iface();
	run_scripts();

	setup_release_on_sigint();

	while(ret >= 0) {
		ret = sleep_lease_time();

		if(ret < 0) break;

		open_udp_socket();
		ret = send_renew_recv_acknak();
		close_socket();

		if(ret < 0) break;

		update_lifetime();
	}

	if(ret == -EINTR) {
		open_raw_socket();
		send_release();
	}

	flush_iface();

	return 0;
}

static void req_request()
{
	open_raw_socket();
	pick_random_xid();

	send_discover_recv_offer();
	send_request_recv_acknak();

	configure_iface();
	save_lease();

	run_scripts();
}

static void req_renew(void)
{
	int ret;

	load_lease();
	open_udp_socket();

	if((ret = send_renew_recv_acknak()) < 0)
		fail(NULL, device, ret);

	update_lifetime();
	save_lease();
}

static void req_release(void)
{
	load_lease();
	open_raw_socket();
	send_release();
	flush_iface();
	delete_lease();
}

static void req_show(void)
{
	load_lease();
	show_config();
}

static void req_cancel(void)
{
	load_lease();
	flush_iface();
	delete_lease();
}

static void req_delete(void)
{
	delete_lease();
}

static const struct cmd {
	char name[16];
	void (*call)(void);
} commands[] = {
	{ "request",   req_request   },
	{ "renew",     req_renew     },
	{ "release",   req_release   },
	{ "cancel",    req_cancel    },
	{ "delete",    req_delete    },
	{ "show",      req_show      }
};

static const struct cmd* find_cmd(char* name)
{
	const struct cmd* cc;

	for(cc = commands; cc < ARRAY_END(commands); cc++)
		if(!strncmp(cc->name, name, sizeof(cc->name)))
			return cc;

	return NULL;
}

int main(int argc, char** argv)
{
	const struct cmd* cc;

	environ = argv + argc + 1;

	if(argc > 1 && argv[1][0] == '-')
		fail("no options allowed", NULL, 0);
	if(argc < 2)
		fail("interface name required", NULL, 0);

	device = argv[1];

	if(argc == 2)
		return lease_wait();
	if(argc > 3)
		fail("too many arguments", NULL, 0);

	if(!(cc = find_cmd(argv[2])))
		fail("unknown command", argv[2], 0);

	cc->call();

	return 0;
}
