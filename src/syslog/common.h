#include <config.h>

#define DEVLOG HERE "/dev/log"

#define LOGDIR HERE "/var/log"         /* <-- must be parent directory for VARLOG */
#define VARLOG HERE "/var/log/syslog"  /* inotify watch code in logcat needs it   */
#define OLDLOG HERE "/var/log/sysold"
