#define CONSOLES 8
#define KEYBOARDS 10
#define INPUTS 128

/* One active VT, with a process running on it.
   Kernel index (N in /dev/ttyN and vtx.tty here) is always
   non-zero and almost never matches indexes in consoles[]. */

struct vtx {
	int ttyfd;    /* /dev/ttyN */
	int ctlfd;    /* control socket */
	int pid;
	short tty;
};

/* VT-bound device handle, opened on behalf of vtx.pid and multiplexed
   during VT switches. We keep track of its dev number (maj:min) since
   different clients will often open the same devices. There's going
   to be lots of these, like 10 or so per vtx. All of them char devices,
   some DRM and some inputs. */

struct vtd {
	int fd;
	int dev;
	short tty;
};

/* Our private handles for keyboard devices which we use to listen
   for VT-switch key combos. The kernel side does not track modifiers
   (because why would it) so we have to do it ourselves. */

struct kbd {
	int fd;
	int dev;
	int mod;
};

extern char* greeter;
extern char** environ;
extern int activetty;
extern int ctlsockfd;

/* Numbers below are upper limits for loops; all arrays may happen
   to have empty slots between used ones. */

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
int switchto(int tty);

void shutdown(void);
void waitpids(void);

void close_dead_vt(struct vtx* cvt);

int lock_switch(void);
int unlock_switch(void);

void engage(void);
void activate(int tty);
void disengage(void);

void close_dead_client(int pid);
int spawn_client(char* cmd);
void spawn_greeter(void);
void setup_greeter(void);

void setup_ctl_socket(void);

void request_fds_update(void);

void mainloop(void);
