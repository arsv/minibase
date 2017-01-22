/* socket.protocol = NETLINK_ROUTE */

/* nlmsg.type */

#define RTM_NEWLINK   16
#define RTM_DELLINK   17
#define RTM_GETLINK   18
#define RTM_SETLINK   19

/* ifinfomsg.flags */

#define IFF_UP           (1<<0)
#define IFF_BROADCAST    (1<<1)
#define IFF_DEBUG        (1<<2)
#define IFF_LOOPBACK     (1<<3)
#define IFF_POINTOPOINT  (1<<4)
#define IFF_NOTRAILERS   (1<<5)
#define IFF_RUNNING      (1<<6)
#define IFF_NOARP        (1<<7)
#define IFF_PROMISC      (1<<8)
#define IFF_ALLMULTI     (1<<9)
#define IFF_MASTER      (1<<10)
#define IFF_SLAVE       (1<<11)
#define IFF_MULTICAST   (1<<12)
#define IFF_PORTSEL     (1<<13)
#define IFF_AUTOMEDIA   (1<<14)
#define IFF_DYNAMIC     (1<<15)
#define IFF_LOWER_UP    (1<<16)
#define IFF_DORMANT     (1<<17)
#define IFF_ECHO        (1<<18)


struct nlmsg;

struct ifinfomsg {
	struct nlmsg nlm;
	uint8_t  family;
	uint8_t  pad0;
	uint16_t type;
	int32_t  index;
	uint32_t flags;
	uint32_t change;
	char payload[];
} __attribute__((packed));
