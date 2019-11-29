#include <dirs.h>

#define CONFDIR HERE "/etc/apphub"
#define CONTROL RUN_CTRL "/apphub"

#define CMD_STATUS     1
#define CMD_SPAWN      2
#define CMD_SIGTERM    4
#define CMD_SIGKILL    5
#define CMD_FETCH      6
#define CMD_FLUSH      7

#define ATTR_NAME      1
#define ATTR_ARG       2
#define ATTR_ENV       3
#define ATTR_XID       4
#define ATTR_PID       5
#define ATTR_PROC      6
#define ATTR_RING      8
#define ATTR_EXIT      9
