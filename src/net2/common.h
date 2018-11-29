#include <dirs.h>

#define IFCTL RUN_CTRL "/ifmon"
#define IFCFG HERE "/var/ifmon"
#define RESOLV_CONF HERE "/run/resolv.conf"

#define IF(c) TAGGED('I', 'F', c)

#define CMD_IF_STATUS       IF(0)
#define CMD_IF_LEASES       IF(0)
#define CMD_IF_TAG_ONLY     IF(1)
#define CMD_IF_TAG_ALSO     IF(2)
#define CMD_IF_TAG_NONE     IF(3)
#define CMD_IF_DHCP_ONCE    IF(5)
#define CMD_IF_DHCP_AUTO    IF(5)
#define CMD_IF_DHCP_STOP    IF(5)

#define REP_IF_DHCP_DONE    IF(1)
#define REP_IF_DHCP_FAIL    IF(2)

#define ATTR_SSID       1
#define ATTR_PSK        2
#define ATTR_PRIO       3
#define ATTR_SIGNAL     4
#define ATTR_FREQ       5
#define ATTR_TYPE       6
#define ATTR_BSSID      7
#define ATTR_SCAN       8
#define ATTR_IFI        9
#define ATTR_NAME      10
#define ATTR_STATE     11
#define ATTR_IP        12
#define ATTR_MASK      13
#define ATTR_LINK      14
#define ATTR_MODE      15
#define ATTR_FLAGS     16
#define ATTR_ADDR      17
#define ATTR_TIME      18

#define IF_ENABLED  (1<<0)
#define IF_CARRIER  (1<<1)
#define IF_RUNNING  (1<<2)
#define IF_STOPPING (1<<3)
#define IF_ERROR    (1<<4)
#define IF_DHCPFAIL (1<<5)

#define IF_MODE_SKIP    0
#define IF_MODE_DOWN    1
#define IF_MODE_DHCP    2
#define IF_MODE_WIFI    3
