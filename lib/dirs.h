/* For development, paths should be ./dev, ./var, ./etc and so on.
   Target builds need proper /dev, /var, /etc. */

#ifdef DEVEL
# define HERE "."
# define RUN_CTRL "./run"
#else
# define HERE ""
# define RUN_CTRL "/run/ctrl"
#endif

/* For nlusctl commands; (a,b) are two chars identifying the service. */

#define TAGGED(a, b, c) ((a << 24) | (b << 16) | (c))
