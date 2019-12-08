#include <dirs.h>

#define CONFDIR HERE "/etc/ptyhub"
#define CONTROL RUN_CTRL "/ptyhub"

#define CMD_STATUS     1
#define CMD_START      2
#define CMD_SPAWN      3
#define CMD_DETACH     4
#define CMD_ATTACH     5
#define CMD_SIGTERM    6
#define CMD_SIGKILL    7

#define REP_EXIT       1

#define ATTR_NAME      1
#define ATTR_ARGV      2
#define ATTR_ENVP      3
#define ATTR_XID       4
#define ATTR_PID       5
#define ATTR_PROC      6
#define ATTR_RING      8
#define ATTR_EXIT      9
#define ATTR_NEXT     10
