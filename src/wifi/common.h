#include <dirs.h>

#define WICTL RUN_CTRL "/wsupp"

#define WI(c) TAGGED('W', 'I', c)

#define CMD_WI_STATUS       WI(0)
#define CMD_WI_SETDEV       WI(1)
#define CMD_WI_SCAN         WI(2)
#define CMD_WI_NEUTRAL      WI(4)
#define CMD_WI_CONNECT      WI(5)
#define CMD_WI_DETACH       WI(6)
#define CMD_WI_RESUME       WI(7)

#define REP_WI_SCAN_END     WI(1)
#define REP_WI_CONNECTING   WI(2)
#define REP_WI_DISCONNECT   WI(3)
#define REP_WI_NO_CONNECT   WI(4)
#define REP_WI_LINK_READY   WI(5)

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
#define ATTR_IES       19
#define ATTR_ERROR     20

#define WS_STOPPED         0
#define WS_MONITOR         1
#define WS_SCANNING        2
#define WS_CONNECTING      3
#define WS_CONNECTED       4
#define WS_RFKILLED        5
#define WS_STOPPING        6
