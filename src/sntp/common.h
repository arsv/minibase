#include <dirs.h>

#define TIMED_CTRL HERE "/run/ctrl/timed"

#define TI(c) TAGGED('T', 'I', c)

#define CMD_TI_STATUS    TI(0)
#define CMD_TI_SERVER    TI(1)
#define CMD_TI_SRLIST    TI(2)
#define CMD_TI_RETRY     TI(3)
#define CMD_TI_RESET     TI(4)
#define CMD_TI_FORCE     TI(5)

#define REP_TI_IDLE      TI(0)
#define REP_TI_SELECT    TI(1)
#define REP_TI_PING      TI(2)
#define REP_TI_POLL      TI(3)

#define ATTR_STATE     1

#define ATTR_SERVER    2
#define ATTR_ADDR      3
#define ATTR_PORT      4
#define ATTR_FAILED    3

#define ATTR_TIMELEFT  5
#define ATTR_NEXTTIME  6
#define ATTR_POLLTIME  7
#define ATTR_SYNCTIME  8
#define ATTR_OFFSET    9 /* time difference from the last packet */
#define ATTR_RTT      10 /* round trip time */
#define ATTR_FAILURES 11
