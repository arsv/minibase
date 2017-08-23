#include <dirs.h>

#define CONFDIR ETCDIR "/sudo"
#define CONTROL RUNDIR "/suhub"

#define SU 0x53550000 /* 'S', 'U', \0, \0 */

#define CMD_EXEC     SU+1
#define CMD_KILL     SU+2

#define REP_DEAD     SU+3

#define ATTR_ARGV      1
#define ATTR_SIGNAL    2
#define ATTR_STATUS    3
