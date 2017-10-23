#include <dirs.h>

#define IFCTL NLCDIR "/ifmon"
#define IFCFG VARDIR "/ifmon"

#define WICTL NLCDIR "/wienc"
#define WICFG VARDIR "/wipsk"

#define NI(c) TAGGED('N', 'I', c)
#define WI(c) TAGGED('W', 'I', c)

#define CMD_WI_STATUS   WI(0)
#define CMD_WI_SCAN     WI(1)
#define CMD_WI_NEUTRAL  WI(2)
#define CMD_WI_CONNECT  WI(3)

#define REP_WI_NET_DOWN   WI(0)
#define REP_WI_SCANNING   WI(1)
#define REP_WI_SCAN_DONE  WI(2)
#define REP_WI_SCAN_FAIL  WI(3)
#define REP_WI_DISCONNECT WI(4)
#define REP_WI_NO_CONNECT WI(5)
#define REP_WI_CONNECTED  WI(6)

#define CMD_NI_STATUS       NI(0)
#define CMD_NI_NEUTRAL      NI(1)
#define CMD_NI_SKIP         NI(2)
#define CMD_NI_DOWN         NI(3)
#define CMD_NI_AUTO         NI(4)
#define CMD_NI_SETIP        NI(5)
#define CMD_NI_WIENC        NI(6)
#define CMD_NI_XDHCP        NI(7)

#define REP_NI_LINK_DOWN    NI(0)
#define REP_NI_LINK_ENABLED NI(1)
#define REP_NI_LINK_CARRIER NI(2)
#define REP_NI_DHCP_FAIL    NI(3)
#define REP_NI_DHCP_DONE    NI(4)

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

#define WS_IDLE         0
#define WS_RFKILLED     1
#define WS_NETDOWN      2
#define WS_SCANNING     3
#define WS_CONNECTING   4
#define WS_CONNECTED    5
