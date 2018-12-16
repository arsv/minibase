#include <bits/types.h>

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
	char options[500];
} __attribute__((packed));

/* Internals */

struct iface {
	int index;
	char name[IFNAMESIZ];
	byte mac[6];
};

struct offer {
	byte srvip[4];
	byte ourip[4];
	byte srvmac[6];
	byte ourmac[6];
};

extern char* device;
extern char** environ;
extern int optptr; /* bytes used in packet.dhcp.options */

extern struct iface iface;
extern struct offer offer;
extern struct dhcpmsg packet;

struct dhcpopt* first_opt(void);
struct dhcpopt* next_opt(struct dhcpopt* curr);
struct dhcpopt* get_option(int code, int len);

void show_config(void);
void configure_iface(void);
void flush_iface(void);
void update_lifetime(void);

/* Wire-level calls */
void pick_random_xid(void);
void open_raw_socket(void);
void open_udp_socket(void);
void close_socket(void);

void put_header(int type);
void put_byte(int code, uint8_t val);
void put_mac(int code, byte mac[6]);
void put_ip(int code, byte ip[4]);

void send_raw_packet(void);
void send_udp_packet(void);
int recv_raw_packet(byte mac[6]);
int recv_udp_packet(void);

struct dhcpopt* first_opt(void);
struct dhcpopt* next_opt(struct dhcpopt* curr);
struct dhcpopt* get_option(int code, int len);
void* get_value(int code, int len);
int get_opt_int(int code);

void load_lease();
void save_lease();
void delete_lease();

void run_scripts(void);
void prepare_iface(int fd);
void check_no_lease(void);
