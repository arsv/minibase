#define CONSOLES 12
#define KEYBOARDS 10
#define INPUTS 256
#define CMDSIZE 16

/* disable() arguments */
#define TEMPORARILY 0
#define PERMANENTLY 1

/* One active VT, with a process running on it.
   Kernel index (N in /dev/ttyN and vtx.tty here) is always
   non-zero and may not match indexes in consoles[]. */

struct vtx {
	int ttyfd;    /* /dev/ttyN */
	int ctlfd;    /* control socket */
	int pid;
	short tty;    /* N in ttyN, always 1-based */
	short pin;    /* the command to this particular tty */
	char cmd[CMDSIZE];
};

/* VT-bound device handle, opened on behalf of vtx.pid and multiplexed
   during VT switches. There's going to be lots of these, like 10 or so
   per vtx. All of them char devices, some DRM and some inputs. */

struct vtd {
	int fd;
	int dev;      /* maj:min, in st_dev encoding */
	short tty;    /* N in ttyN this fd is associated with */
};

/* Our private handles for keyboard devices which we use to listen
   for VT-switch key combos. The kernel side does not track modifiers
   (because why would it) so we have to do it ourselves. */

struct kbd {
	int fd;
	int dev;      /* maj:min again */
	int mod;      /* active modifiers bitmask */
};

extern char** environ;
extern int activetty;
extern int initialtty;
extern int pollready;

/* The numbers below are upper limits for loops, all arrays
   may happen to have empty slots below resp. limits. */

extern struct vtx consoles[CONSOLES];
extern struct vtd vtdevices[INPUTS];
extern struct kbd keyboards[KEYBOARDS];

extern int nconsoles;
extern int nvtdevices;
extern int nkeyboards;

/* Cross-module prototypes */

void setup_signals(void);
void setup_keyboards(void);
void handlectl(int vi, int fd);
void handlekbd(int ki, int fd);

int lock_switch(int* mask);
int unlock_switch(void);
int activate(int tty);
void closevt(struct vtx* cvt, int keepvt);
void disable(struct vtd* md, int drop);

void shutdown(void);
void waitpids(void);

int switchto(int tty);
int spawn(char* cmd);
int invoke(struct vtx* cvt);
void setup_pinned(char* greeter, int n, char** cmds, int spareinitial);

void mainloop(void);
