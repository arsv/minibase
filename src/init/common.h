#include <dirs.h>

#define BOOTDIR HERE "/etc/boot"
#define INITDIR HERE "/etc/init"
#define CONTROL RUN_CTRL "/svchub"

#define CMD_LIST       1

#define CMD_RELOAD     2
#define CMD_REBOOT     3
#define CMD_SHUTDOWN   4
#define CMD_POWEROFF   5

#define CMD_STATUS     7
#define CMD_GETBUF     8
#define CMD_START      9
#define CMD_STOP      10
#define CMD_RESET     11
#define CMD_HUP       12
#define CMD_FLUSH     13

#define REP_DIED       1

#define ATTR_PROC      1
#define ATTR_NAME      2
#define ATTR_CODE      3
#define ATTR_PID       4
#define ATTR_RING      5
#define ATTR_EXIT      6
#define ATTR_TIME      7
#define ATTR_NEXT      8
