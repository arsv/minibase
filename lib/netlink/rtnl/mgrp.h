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

#define RTMGRP_ND_USEROPT       (1<<19)
