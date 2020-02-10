/* For development, paths should be ./dev, ./var, ./etc and so on.
   Target builds need proper /dev, /var, /etc. */

#ifdef DEVEL
# define HERE "."
# define BASE_ETC "./etc"
# define BASE_VAR "./var"
# define RUN_CTRL "./run"
# define SYS_ROOT "."
#else
# define HERE ""
# define BASE_ETC "/base/etc"
# define BASE_VAR "/var/base"
# define RUN_CTRL "/run/ctrl"
# define SYS_ROOT "/"
#endif
