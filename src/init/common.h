#include <dirs.h>

#define CONFDIR ETCDIR "/rc"
#define CONTROL RUNDIR "/super"

#define SS 0x53530000

#define CMD_RESET     SS+0
#define CMD_LIST      SS+1
#define CMD_STATUS    SS+2
#define CMD_GETPID    SS+3
#define CMD_FLUSH     SS+4
#define CMD_QUERY     SS+5
#define CMD_ENABLE    SS+6
#define CMD_DISABLE   SS+7
#define CMD_RESTART   SS+8
#define CMD_PAUSE     SS+9
#define CMD_RESUME    SS+10
#define CMD_HUP       SS+11
#define CMD_RELOAD    SS+12
#define CMD_REBOOT    SS+13
#define CMD_SHUTDOWN  SS+14
#define CMD_POWEROFF  SS+15

#define ATTR_PROC     1
#define ATTR_NAME     2
#define ATTR_CODE     3
#define ATTR_PID      4
#define ATTR_RING     5
#define ATTR_EXIT     6
#define ATTR_TIME     7
