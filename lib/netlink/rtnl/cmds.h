/* socket.protocol = NETLINK_ROUTE */

/* Multicast groups */

#define RTMGRP_LINK             (1<<0)
#define RTMGRP_NOTIFY           (1<<1)
#define RTMGRP_NEIGH            (1<<2)
#define RTMGRP_TC               (1<<3)

#define RTMGRP_IPV4_IFADDR      (1<<4)
#define RTMGRP_IPV4_MROUTE      (1<<5)
#define RTMGRP_IPV4_ROUTE       (1<<6)
#define RTMGRP_IPV4_RULE        (1<<7)

#define RTMGRP_IPV6_IFADDR      (1<<8)
#define RTMGRP_IPV6_MROUTE      (1<<9)
#define RTMGRP_IPV6_ROUTE       (1<<10)
#define RTMGRP_IPV6_IFINFO      (1<<11)

/* Message types, nlmsg.type */

#define RTM_NEWLINK	16
#define RTM_DELLINK	17
#define RTM_GETLINK	18
#define RTM_SETLINK	19

#define RTM_NEWADDR	20
#define RTM_DELADDR	21
#define RTM_GETADDR	22

#define RTM_NEWROUTE	24
#define RTM_DELROUTE	25
#define RTM_GETROUTE	26

#define RTM_NEWNEIGH	28
#define RTM_DELNEIGH	29
#define RTM_GETNEIGH	30

#define RTM_NEWRULE	32
#define RTM_DELRULE	33
#define RTM_GETRULE	34

#define RTM_NEWQDISC	36
#define RTM_DELQDISC	37
#define RTM_GETQDISC	38

#define RTM_NEWTCLASS	40
#define RTM_DELTCLASS	41
#define RTM_GETTCLASS	42

#define RTM_NEWTFILTER	44
#define RTM_DELTFILTER	45
#define RTM_GETTFILTER	46

#define RTM_NEWACTION   48
#define RTM_DELACTION   49
#define RTM_GETACTION   50

#define RTM_NEWPREFIX	 52
#define RTM_GETMULTICAST 58
#define RTM_GETANYCAST	 62

#define RTM_NEWNEIGHTBL	 64
#define RTM_GETNEIGHTBL	 66
#define RTM_SETNEIGHTBL	 67

#define RTM_NEWNDUSEROPT 68

#define RTM_NEWADDRLABEL 72
#define RTM_DELADDRLABEL 73
#define RTM_GETADDRLABEL 74

#define RTM_GETDCB      78
#define RTM_SETDCB      79

#define RTM_NEWNETCONF  80
#define RTM_GETNETCONF  82

#define RTM_NEWMDB      84
#define RTM_DELMDB      85
#define RTM_GETMDB      86

#define RTM_NEWNSID     88
#define RTM_DELNSID     89
#define RTM_GETNSID     90

#define RTM_NEWSTATS    92
#define RTM_GETSTATS    94
