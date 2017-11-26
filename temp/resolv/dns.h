#define DNSF_QR (1<<15)
#define DNSF_OP (7<<11)
# define DNSF_OP_QUERY  (0<<11)
# define DNSF_OP_STATUS (1<<11)
# define DNSF_OP_NOTIFY (4<<11)
# define DNSF_OP_UPDATE (5<<11)
#define DNSF_AA (1<<10)
#define DNSF_TC (1<<9)
#define DNSF_RD (1<<8)
#define DNSF_RA (1<<7)
#define DNSF_Z  (0<<4)
#define DNSF_RC (15<<0)
# define DNSF_RC_SUCCESS (0<<0)
# define DNSF_RC_FORMAT  (1<<0)
# define DNSF_RC_SERVER  (2<<0)
# define DNSF_RC_NAME    (3<<0)
# define DNSF_RC_NOTIMPL (4<<0)
# define DNSF_RC_REFUSED (5<<0)
# define DNSF_RC_NOTAUTH (9<<0)
# define DNSF_RC_NOTZONE (10<<0)

#define DNS_TYPE_A     1
#define DNS_TYPE_CNAME 5
#define DNS_TYPE_SOA   6
#define DNS_TYPE_PTR  12

#define DNS_CLASS_IN 1

struct dnshdr {
	ushort id;
	ushort flags;

	ushort qdcount;
	ushort ancount;
	ushort nscount;
	ushort arcount;
} __attribute__((packed));

struct dnsres {
	ushort type;
	ushort class;
	uint ttl;
	ushort length;
	byte data[];
} __attribute__((packed));
