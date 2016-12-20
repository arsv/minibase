#ifndef __BITS_NETLINK_H__
#define __BITS_NETLINK_H__

#include <bits/types.h>

#define NETLINK_ROUTE           0
#define NETLINK_NFLOG           5
#define NETLINK_NETFILTER       12
#define NETLINK_IP6_FW          13
#define NETLINK_KOBJECT_UEVENT  15
#define NETLINK_GENERIC         16

#define NLM_F_REQUEST        (1<<0)
#define NLM_F_MULTI          (2<<1)
#define NLM_F_ACK            (1<<2)
#define NLM_F_ECHO           (1<<3)
#define NLM_F_DUMP_INTR      (1<<4)
#define NLM_F_DUMP_FILTERED  (1<<5)

struct sockaddr_nl {
        unsigned short  nl_family;
        unsigned short  nl_pad;
        uint32_t        nl_pid;
        uint32_t        nl_groups;
};

#endif
