#include <dirs.h>

#define IFCTL RUN_CTRL "/ifmon"
#define ETCNET HERE "/etc/net"

#define IF(c) TAGGED('I', 'F', c)

#define CMD_IF_STATUS	   IF(0)
#define CMD_IF_MODE	   IF(1)
#define CMD_IF_STOP	   IF(2)
#define CMD_IF_KILL	   IF(3)
#define CMD_IF_DROP	   IF(4)

#define CMD_IF_DHCP_AUTO   IF(5)
#define CMD_IF_DHCP_ONCE   IF(6)
#define CMD_IF_DHCP_STOP   IF(7)
#define CMD_IF_RECONNECT   IF(8)

#define REP_IF_MODE        IF(1)
#define REP_IF_STOP        IF(2)

#define ATTR_LINK  1
#define ATTR_IFI   2
#define ATTR_NAME  3
#define ATTR_MODE  4
#define ATTR_STATE 5
#define ATTR_ERRNO 6
#define ATTR_FLAGS 7
#define ATTR_XCODE 8
#define ATTR_PID   9

/* ATTR_STATE above */

#define IF_STATE_MASK         0x0F
#define IF_STATE_IDEF         0x00
#define IF_STATE_MODE         0x01
#define IF_STATE_STOP         0x02
#define IF_STATE_DHCP         0x03

#define IF_FLAG_ENABLED     (1<<4)
#define IF_FLAG_CARRIER     (1<<5)
#define IF_FLAG_MARKED      (1<<6)
#define IF_FLAG_AUTO_DHCP   (1<<7)
#define IF_FLAG_DHCP_ONCE   (1<<8)

#define IF_FLAG_RUNNING    (1<<10)
#define IF_FLAG_STATUS     (1<<11)
#define IF_FLAG_FAILED     (1<<12)
