#include <dirs.h>

#define CONTROL RUN_CTRL "/ifmon"
#define ETCNET HERE "/etc/net"

#define CMD_STATUS      0
#define CMD_IDMODE      1
#define CMD_MODE        2
#define CMD_STOP        3
#define CMD_KILL        4
#define CMD_DROP        5

#define CMD_DHCP_AUTO   6
#define CMD_DHCP_ONCE   7
#define CMD_DHCP_STOP   8
#define CMD_RECONNECT   9

#define REP_MODE        1
#define REP_STOP        2

#define ATTR_LINK       1
#define ATTR_IFI        2
#define ATTR_NAME       3
#define ATTR_MODE       4
#define ATTR_STATE      5
#define ATTR_ERRNO      6
#define ATTR_FLAGS      7
#define ATTR_XCODE      8
#define ATTR_PID        9

/* ATTR_STATE above */

#define IF_STATE_MASK         0x0F
#define IF_STATE_IDEF         0x00
#define IF_STATE_MODE         0x01
#define IF_STATE_STOP         0x02
#define IF_STATE_DHCP         0x03
#define IF_STATE_POKE         0x04

#define IF_FLAG_ENABLED     (1<<4)
#define IF_FLAG_CARRIER     (1<<5)
#define IF_FLAG_MARKED      (1<<6)
#define IF_FLAG_AUTO_DHCP   (1<<7)
#define IF_FLAG_DHCP_ONCE   (1<<8)

#define IF_FLAG_RUNNING    (1<<10)
#define IF_FLAG_STATUS     (1<<11)
#define IF_FLAG_FAILED     (1<<12)
