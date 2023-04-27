#include <config.h>

#define BOOTDIR BASE_ETC "/boot"
#define INITDIR BASE_ETC "/init"
#define CONTROL RUN_CTRL "/svchub"

#define CMD_LIST       1
#define CMD_STATUS     2
#define CMD_GETBUF     3

#define CMD_FLUSH      4
#define CMD_REMOVE     5

#define CMD_STOP       8
#define CMD_SIGHUP     9

#define CMD_REBOOT    12
#define CMD_SHUTDOWN  13
#define CMD_POWEROFF  14

#define CMD_START     16
#define CMD_SPAWN     17
#define CMD_STOUT     18

#define REP_DIED       1

#define ATTR_PROC      1
#define ATTR_NAME      2
#define ATTR_CODE      3
#define ATTR_PID       4
#define ATTR_RING      5
#define ATTR_EXIT      6
#define ATTR_NEXT      7
