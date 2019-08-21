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

#define REP_IF_DONE	   IF(1)

#define ATTR_LINK  1
#define ATTR_IFI   2
#define ATTR_NAME  3
#define ATTR_MODE  4
#define ATTR_STATE 5
#define ATTR_ERRNO 6
#define ATTR_STATUS 7
