/* socket.protocol = NETLINK_ROUTE */

/* nlmsg.type */

#define RTM_NEWADDR	20
#define RTM_DELADDR	21
#define RTM_GETADDR	22

/* ifaddrmsg.flags */

#define IFA_F_SECONDARY        (1<<0)
#define	IFA_F_NODAD            (1<<1)
#define IFA_F_OPTIMISTIC       (1<<2)
#define IFA_F_DADFAILED        (1<<3)
#define	IFA_F_HOMEADDRESS      (1<<4)
#define IFA_F_DEPRECATED       (1<<5)
#define IFA_F_TENTATIVE        (1<<6)
#define IFA_F_PERMANENT        (1<<7)
#define IFA_F_MANAGETEMPADDR   (1<<8)
#define IFA_F_NOPREFIXROUTE    (1<<9)
#define IFA_F_MCAUTOJOIN      (1<<10)
#define IFA_F_STABLE_PRIVACY  (1<<11)

/* nlattr.type */

#define IFA_ADDRESS     1
#define IFA_LOCAL       2
#define IFA_LABEL       3
#define IFA_BROADCAST   4
#define IFA_ANYCAST     5
#define IFA_CACHEINFO   6
#define IFA_MULTICAST   7
#define IFA_FLAGS       8

struct nlmsg;

struct ifaddrmsg {
	struct nlmsg nlm;
	uint8_t family;
	uint8_t prefixlen;
	uint8_t flags;
	uint8_t scope;
	int index;
	char payload[];
} __attribute__((packed));
