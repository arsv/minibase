#include "config.h"

/* disable() arguments */
#define TEMPORARILY 0
#define PERMANENTLY 1

/* One active VT, with a process running on it.
   Kernel index (N in /dev/ttyN and vtx.tty here) is always
   non-zero and may not match indexes in consoles[]. */

struct term {
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

struct mdev {
	int fd;
	int dev;      /* maj:min, in st_dev encoding */
	short tty;    /* N in ttyN this fd is associated with */
};

struct conn {
	int fd;
};

struct ucmsg;

extern char** environ;
extern int activetty;
extern int initialtty;
extern int pollset;
extern int ctrlfd;

/* The numbers below are upper limits for loops, all arrays
   may happen to have empty slots below resp. limits. */

extern struct term terms[NTERMS];
extern struct mdev mdevs[NMDEVS];
extern struct conn conns[NCONNS];
extern int nterms;
extern int nmdevs;
extern int nconns;

/* Cross-module prototypes */

void setup_signals(void);

void notify_activated(int tty);
void notify_deactivated(int tty);

int dispatch_cmd(struct conn* cn, struct ucmsg* msg);

int lock_switch(int* mask);
int unlock_switch(void);
int activate(int tty);
void closevt(struct term* cvt, int keepvt);
void disable(struct mdev* md, int drop);

void shutdown(void);
void waitpids(void);

int switchto(int tty);
int spawn(char* cmd);
int invoke(struct term* cvt);
void setup_pinned(char* greeter, int n, char** cmds, int spareinitial);
int prep_event_dev(int fd);

struct term* grab_term_slot(void);
struct conn* grab_conn_slot(void);
struct mdev* grab_mdev_slot(void);
struct term* find_term_by_pid(int pid);
struct term* find_term_by_tty(int tty);

int wait_poll(void);

void accept_ctrl(void);
void setup_ctrl(void);
void handle_conn(struct conn* cn);

void handle_pipe(struct term* vt);
