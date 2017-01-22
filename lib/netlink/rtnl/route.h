#include <bits/types.h>
#include <bits/arp.h>

/* nlmsg.type */

#define RTM_NEWROUTE   24
#define RTM_DELROUTE   25
#define RTM_GETROUTE   26

/* rtmsg.type */

#define RTN_UNSPEC      0
#define RTN_UNICAST     1
#define RTN_LOCAL       2
#define RTN_BROADCAST   3
#define RTN_ANYCAST     4
#define RTN_MULTICAST   5
#define RTN_BLACKHOLE   6
#define RTN_UNREACHABLE 7
#define RTN_PROHIBIT    8
#define RTN_THROW       9
#define RTN_NAT        10
#define RTN_XRESOLVE   11

/* rtmsg.protocol */

#define RTPROT_UNSPEC   0
#define RTPROT_KERNEL   2
#define RTPROT_BOOT     3
#define RTPROT_STATIC   4
#define RTPROT_DHCP    16

/* rtmsg.scope */

#define RT_SCOPE_UNIVERSE     0
#define RT_SCOPE_SITE       200
#define RT_SCOPE_LINK       253
#define RT_SCOPE_HOST       254
#define RT_SCOPE_NOWHERE    255

/* rtmsg.flags */

#define RTM_F_NOTIFY         (1<<8)
#define RTM_F_CLONED         (1<<9)
#define RTM_F_EQUALIZE      (1<<10)
#define RTM_F_PREFIX        (1<<11)
#define RTM_F_LOOKUP_TABLE  (1<<12)

/* rtmsg.table */

#define RT_TABLE_UNSPEC       0
#define RT_TABLE_DEFAULT    253
#define RT_TABLE_MAIN       254
#define RT_TABLE_LOCAL      255

/* nlattr.type */

#define RTA_UNSPEC         0
#define RTA_DST            1
#define RTA_SRC            2
#define RTA_IIF            3
#define RTA_OIF            4
#define RTA_GATEWAY        5
#define RTA_PRIORITY       6
#define RTA_PREFSRC        7
#define RTA_METRICS        8
#define RTA_MULTIPATH      9
#define RTA_FLOW          11
#define RTA_CACHEINFO     12
#define RTA_TABLE         15
#define RTA_MARK          16
#define RTA_MFC_STATS     17
#define RTA_VIA           18
#define RTA_NEWDST        19
#define RTA_PREF          20
#define RTA_ENCAP_TYPE    21
#define RTA_ENCAP         22
#define RTA_EXPIRES       23
#define RTA_PAD           24

struct nlmsg;

struct rtmsg {
	struct nlmsg nlm;

	uint8_t family;
	uint8_t dst_len;
	uint8_t src_len;
	uint8_t tos;

	uint8_t table;
	uint8_t protocol;
	uint8_t scope;
	uint8_t type;

	uint32_t flags;

	char payload[];
} __attribute__((packed));
