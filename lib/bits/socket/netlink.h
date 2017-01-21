#include <bits/types.h>

#define AF_NETLINK 16
#define PF_NETLINK 16

/* level and options for setsockopt() */
#define SOL_NETLINK 270
#define NETLINK_ADD_MEMBERSHIP 1

/* protocols for socket() */
#define NETLINK_ROUTE           0
#define NETLINK_NFLOG           5
#define NETLINK_NETFILTER       12
#define NETLINK_IP6_FW          13
#define NETLINK_KOBJECT_UEVENT  15
#define NETLINK_GENERIC         16

struct sockaddr_nl {
        uint16_t family;
        uint16_t pad;
        uint32_t pid;
        uint32_t groups;
};
