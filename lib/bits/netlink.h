#ifndef __BITS_NETLINK_H__
#define __BITS_NETLINK_H__

#include <bits/types.h>

#define NETLINK_ROUTE           0
#define NETLINK_NFLOG           5
#define NETLINK_NETFILTER       12
#define NETLINK_IP6_FW          13
#define NETLINK_KOBJECT_UEVENT  15

struct sockaddr_nl {
        unsigned short  nl_family;
        unsigned short  nl_pad;
        uint32_t        nl_pid;
        uint32_t        nl_groups;
};

#endif
