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

/* nlattr.type */

#define IFLA_ADDRESS          1
#define IFLA_BROADCAST        2
#define IFLA_IFNAME           3
#define IFLA_MTU              4
#define IFLA_LINK             5
#define IFLA_QDISC            6
#define IFLA_STATS            7
#define IFLA_COST             8
#define IFLA_PRIORITY         9
#define IFLA_MASTER          10
#define IFLA_WIRELESS        11
#define IFLA_PROTINFO        12
#define IFLA_TXQLEN          13
#define IFLA_MAP             14
#define IFLA_WEIGHT          15
#define IFLA_OPERSTATE       16
#define IFLA_LINKMODE        17
#define IFLA_LINKINFO        18
#define IFLA_NET_NS_PID      19
#define IFLA_IFALIAS         20
#define IFLA_NUM_VF          21
#define IFLA_VFINFO_LIST     22
#define IFLA_STATS64         23
#define IFLA_VF_PORTS        24
#define IFLA_PORT_SELF       25
#define IFLA_AF_SPEC         26
#define IFLA_GROUP           27
#define IFLA_NET_NS_FD       28
#define IFLA_EXT_MASK        29
#define IFLA_PROMISCUITY     30
#define IFLA_NUM_TX_QUEUES   31
#define IFLA_NUM_RX_QUEUES   32
#define IFLA_CARRIER         33
#define IFLA_PHYS_PORT_ID    34
#define IFLA_CARRIER_CHANGES 35
#define IFLA_PHYS_SWITCH_ID  36
#define IFLA_LINK_NETNSID    37
#define IFLA_PHYS_PORT_NAME  38
#define IFLA_PROTO_DOWN      39
#define IFLA_GSO_MAX_SEGS    40
#define IFLA_GSO_MAX_SIZE    41
#define IFLA_PAD             42
#define IFLA_XDP             43

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
