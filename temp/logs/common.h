#include <dirs.h>

#define DEVLOG DEVDIR "/log"

#define LOGDIR VARDIR "/log"     /* <-- must be parent directory for VARLOG */
#define VARLOG LOGDIR "/syslog"  /* inotify watch code in logcat needs it   */
#define OLDLOG LOGDIR "/sysold"
