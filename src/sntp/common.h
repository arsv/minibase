#include <config.h>

#define CONTROL RUN_CTRL "/timed"

#define CMD_STATUS     1
#define CMD_SERVER     2
#define CMD_SRLIST     3
#define CMD_RETRY      4
#define CMD_RESET      5
#define CMD_FORCE      6

#define REP_IDLE       1
#define REP_SELECT     2
#define REP_PING       3
#define REP_POLL       4

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
