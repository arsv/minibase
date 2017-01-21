/* socket.protocol = NETLINK_GENERIC, nlmsg.type = GENL_ID_CTRL */

#define GENL_ID_CTRL        0x10

/* Commands */

#define CTRL_CMD_UNSPEC        0
#define CTRL_CMD_NEWFAMILY     1
#define CTRL_CMD_DELFAMILY     2
#define CTRL_CMD_GETFAMILY     3
#define CTRL_CMD_NEWOPS        4
#define CTRL_CMD_DELOPS        5
#define CTRL_CMD_GETOPS        6
#define CTRL_CMD_NEWMCAST_GRP  7
#define CTRL_CMD_DELMCAST_GRP  8
#define CTRL_CMD_GETMCAST_GRP  9

/* Attributes */

#define CTRL_ATTR_UNSPEC       0
#define CTRL_ATTR_FAMILY_ID    1
#define CTRL_ATTR_FAMILY_NAME  2
#define CTRL_ATTR_VERSION      3 
#define CTRL_ATTR_HDRSIZE      4
#define CTRL_ATTR_MAXATTR      5
#define CTRL_ATTR_OPS          6
#define CTRL_ATTR_MCAST_GROUPS 7
