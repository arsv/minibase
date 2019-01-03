#include <dirs.h>

#define BOOTDIR HERE "/etc/boot"
#define INITDIR HERE "/etc/init"
#define CONTROL RUN_CTRL "/svhub"

#define SV(c) TAGGED('S', 'V', c)

#define CMD_LIST      SV(1)
#define CMD_RELOAD    SV(2)
#define CMD_REBOOT    SV(3)
#define CMD_SHUTDOWN  SV(4)
#define CMD_POWEROFF  SV(5)
#define CMD_FLUSHALL  SV(6)

#define CMD_STATUS    SV(7)
#define CMD_GETPID    SV(8)
#define CMD_START     SV(9)
#define CMD_STOP      SV(10)
#define CMD_PAUSE     SV(11)
#define CMD_RESUME    SV(12)
#define CMD_HUP       SV(13)
#define CMD_FLUSH     SV(14)
#define CMD_SHOW      SV(15)

#define ATTR_PROC      1
#define ATTR_NAME      2
#define ATTR_CODE      3
#define ATTR_PID       4
#define ATTR_RING      5
#define ATTR_EXIT      6
#define ATTR_TIME      7
