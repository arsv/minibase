#ifdef DEVEL
#define CONFDIR "./rc"
#define CONTROL "./ctl"
#else
#define CONFDIR "/etc/rc"
#define CONTROL "/run/super"
#endif

#define NAMELEN 19
#define NPROCS 90
#define NCONNS 10
#define NPREQS 10

#define STABLE_TRESHOLD 5
#define TIME_TO_RESTART 1
#define TIME_TO_SIGKILL 2
#define TIME_TO_SKIP    5

#define PAGE 4096
#define RINGSIZE 1024
