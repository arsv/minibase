#include <bits/types.h>
#include <bits/time.h>
#include <cdefs.h>

#define ST_DISCOVER 1
#define ST_REQUEST  2
#define ST_LEASED   3
#define ST_RENEWING 4
#define ST_DECONF   5
#define ST_RELEASED 6

/* RFC 951 */

#define BOOT_SERVER_PORT  67
#define BOOT_CLIENT_PORT  68

#define BOOTREQUEST    1
#define BOOTREPLY      2

/* RFC 2132 */

#define DHCPDISCOVER   1
#define DHCPOFFER      2
#define DHCPREQUEST    3
#define DHCPDECLINE    4
#define DHCPACK        5
#define DHCPNAK        6
#define DHCPRELEASE    7
#define DHCPINFORM     8

/* Options from RFC 2132 that the code here somehow depends on.
   Everything else is handled by generic routines that only need
   option codes. */

#define DHCP_COOKIE 0x63825363

#define DHCP_NETMASK        1
#define DHCP_ROUTER_IP      3
#define DHCP_NAME_SERVERS   6
#define DHCP_TIME_SERVERS  42
#define DHCP_REQUESTED_IP  50
#define DHCP_LEASE_TIME    51
#define DHCP_RENEW_TIME    58
#define DHCP_MESSAGE_TYPE  53
#define DHCP_SERVER_ID     54
#define DHCP_CLIENT_ID     61

/* IP headers */

#define IPVERSION 4
#define IPV4IHL5 0x45 /* version 4, header len 5x32bit */
#define IPPROTO_UDP 17
#define IPDEFTTL 64

#define IFNAMESIZ 16

#define ARPHRD_NETROM  0
#define ETH_P_IP  0x0800

struct iphdr {
	uint8_t  verihl;
	uint8_t  tos;
	uint16_t tot_len;
	uint16_t id;
	uint16_t frag_off;
	uint8_t  ttl;
	uint8_t  protocol;
	uint16_t check;
	uint8_t saddr[4];
	uint8_t daddr[4];
} __attribute__((packed));

struct udphdr {
	uint16_t source;
	uint16_t dest;
	uint16_t len;
	uint16_t check;
} __attribute__((packed));

/* RFC 2131 */

struct dhcphdr {
	uint8_t op;
	uint8_t htype;
	uint8_t hlen;
	uint8_t hops;
	uint32_t xid;
	uint16_t secs;
	uint16_t flags;
	uint8_t ciaddr[4];
	uint8_t yiaddr[4];
	uint8_t siaddr[4];
	uint8_t giaddr[4];
	uint8_t chaddr[16];
	char sname[64];
	char file[128];
	uint32_t cookie;
} __attribute__((packed));

/* RFC 2132 */

struct dhcpopt {
	uint8_t code;
	uint8_t len;
	uint8_t payload[];
} __attribute__((packed));

struct dhcpmsg {
	struct iphdr ip;
	struct udphdr udp;
	struct dhcphdr dhcp;
	char options[];
} __attribute__((packed));

struct dhcmd {
	void* buf;
	uint ptr;
	uint max;
};

#define CTX struct top* ctx __unused
#define MSG struct dhcpmsg* msg
#define DH struct dhcmd* dh

#define OPTS "qp"
#define OPT_q (1<<0)
#define OPT_p (1<<1)

struct top {
	int opts;
	char** environ;
	int sigfd;
	int rawfd;
	int nlfd;

	char* ifname;
	int ifindex;

	int state;
	int count;
	int next;
	struct timespec ts;

	uint xid;
	uint seq;

	uint64_t origref;
	uint64_t timeref;
	uint lease_time;
	uint renew_time;

	byte srvip[4];
	byte ourip[4];
	byte srvmac[6];
	byte ourmac[6];

	int pid;
	uint script;
	uint optlen;
	byte options[500];
};

void quit(CTX, char* msg, int err) noreturn;

void start_discover(CTX);
void timeout_waiting(CTX);
void recv_incoming(CTX);
void config_iface(CTX);
void deconf_iface(CTX);
void update_iface(CTX);
void dump_packet(MSG, int optlen);
void check_child(CTX);
void kill_wait_pid(CTX);
void release_address(CTX);

void* get_msg_opt(MSG, int optlen, int code, int size);
void* get_ctx_opt(CTX, int code, int size);
int get_ctx_net_prefix_bits(CTX);

void proceed_with_scripts(CTX);
struct dhcpopt* get_ctx_option(CTX, int code);
void output_lease_info(CTX);
