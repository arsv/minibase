#define CONFDIR "/etc/vts"

#ifdef DEVEL
# define CONTROL "./run/vtmux"
#else
# define CONTROL "/run/vtmux"
#endif

#define CMDSIZE 20

#define NTERMS 12
#define NCONNS 10
#define NMDEVS 60

//#define KEYBOARDS 10
//#define INPUTS 256
//#define CMDSIZE 16
