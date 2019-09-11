#include <dirs.h>

#define CONFDIR HERE "/etc/sudo"
#define CONTROL RUN_CTRL "/suhub"

#define SH(c) TAGGED('S', 'H', c)

#define CMD_EXEC  SH(1)
#define CMD_KILL  SH(2)
#define REP_DEAD  SH(3)

#define ATTR_ARGV     1
#define ATTR_SIGNAL   2
#define ATTR_STATUS   3
