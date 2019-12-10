#include <dirs.h>

#define CONTROL RUN_CTRL "/wsupp"

#define CMD_STATUS       0
#define CMD_SETDEV       1
#define CMD_RUNSCAN      2
#define CMD_GETSCAN      3
#define CMD_NEUTRAL      4
#define CMD_CONNECT      5
#define CMD_DETACH       6
#define CMD_RESUME       7

#define REP_SCAN_END     1
#define REP_DISCONNECT   2
#define REP_NO_CONNECT   3
#define REP_LINK_READY   4

#define ATTR_SSID        1
#define ATTR_PSK         2
#define ATTR_PRIO        3
#define ATTR_SIGNAL      4
#define ATTR_FREQ        5
#define ATTR_TYPE        6
#define ATTR_BSSID       7
#define ATTR_SCAN        8
#define ATTR_IFI         9
#define ATTR_NAME       10
#define ATTR_STATE      11
#define ATTR_IP         12
#define ATTR_MASK       13
#define ATTR_LINK       14
#define ATTR_MODE       15
#define ATTR_FLAGS      16
#define ATTR_ADDR       17
#define ATTR_TIME       18
#define ATTR_IES        19
#define ATTR_ERROR      20
#define ATTR_NEXT       21

#define WS_STOPPED       0
#define WS_MONITOR       1
#define WS_SCANNING      2
#define WS_CONNECTING    3
#define WS_CONNECTED     4
#define WS_RFKILLED      5
#define WS_STOPPING      6
