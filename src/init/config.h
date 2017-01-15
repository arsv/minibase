#define MAXRECS 100
#define NAMELEN 19

#define PAGE 4096

#ifdef DEVEL
#define SVDIR "./rc"
#define SVCTL "./ctl"
#else
#define SVDIR "/etc/rc"
#define SVCTL "/run/svcmon"
#endif

#define SVCTL_TIMEOUT 2

#define TIME_TO_RESTART 1
#define TIME_TO_SIGKILL 2
#define TIME_TO_SKIP    5

#define BOOTCLOCKOFFSET 1000
