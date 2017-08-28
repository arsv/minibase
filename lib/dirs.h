#ifdef DEVEL
# define DEVDIR "dev"
# define ETCDIR "etc"
# define RUNDIR "run"
# define VARDIR "var"
# define NLCDIR "run"
#else
# define DEVDIR "/dev"
# define ETCDIR "/etc"
# define RUNDIR "/run"
# define VARDIR "/var"
# define NLCDIR "/run/ctrl"
#endif

/* For nlusctl commands; (a,b) are two chars identifyint the service. */

#define TAGGED(a, b, c) ((a) | (b << 8) | (c << 16))
