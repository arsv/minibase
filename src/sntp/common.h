#include <dirs.h>

#define TIMED_CTRL HERE "/run/ctrl/timed"

#define TI(c) TAGGED('T', 'I', c)

#define CMD_TI_STATUS    TI(0)
#define CMD_TI_SERVER    TI(1)
#define CMD_TI_RETRY     TI(2)
#define CMD_TI_RESET     TI(3)

#define ATTR_STATE   1

#define ATTR_SERVER  2
#define ATTR_ADDR    3
#define ATTR_PORT    4

#define ATTR_TIMELEFT 5
#define ATTR_NEXTTIME 6
#define ATTR_POLLTIME 7
#define ATTR_SYNCTIME 8
#define ATTR_OFFSET   9 /* time difference from the last packet */
#define ATTR_RTT     10 /* round trip time */

#define TS_IDLE       0
#define TS_SELECT     1
#define TS_PING_SENT  2
#define TS_PING_WAIT  3
#define TS_POLL_SENT  4
#define TS_POLL_WAIT  5
