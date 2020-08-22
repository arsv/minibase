#include <config.h>

#define CONTROL RUN_CTRL "/vtmux"
#define CONFDIR BASE_ETC "/vtmux"

#define CMD_STATUS   1
#define CMD_SWITCH   2
#define CMD_SPAWN    3
#define CMD_SWBACK   4
#define CMD_SWLOCK   5
#define CMD_UNLOCK   6

#define ATTR_VT      1
#define ATTR_TTY    10
#define ATTR_PID    11
#define ATTR_NAME   12
#define ATTR_GRAPH  13
