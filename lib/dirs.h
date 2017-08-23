#ifdef DEVEL
# define HERE "."
#else
# define HERE
#endif

#define DEVDIR HERE "/dev"
#define ETCDIR HERE "/etc"
#define RUNDIR HERE "/run"
#define VARDIR HERE "/var"

/* For nlusctl commands; (a,b) are two chars identifyint the service. */

#define TAGGED(a, b, c) ((a) | (b << 8) | (c << 16))
