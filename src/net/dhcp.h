#include <bits/ints.h>

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
#define DHCP_REQUESTED_IP  50
#define DHCP_LEASE_TIME    51
#define DHCP_MESSAGE_TYPE  53
#define DHCP_SERVER_ID     54
#define DHCP_CLIENT_ID     61

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

struct dhcpopt* first_opt(void);
struct dhcpopt* next_opt(struct dhcpopt* curr);
struct dhcpopt* get_option(int code, int len);

void show_config(uint8_t* ip);
void conf_netdev(int ifi, uint8_t* ip, int skipgw);
