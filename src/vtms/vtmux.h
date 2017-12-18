#include <cdefs.h>

#define CMDSIZE 20

#define NTERMS 12
#define NCONNS 10
#define NMDEVS 60

/* disable() arguments */
#define TEMPORARILY 0
#define PERMANENTLY 1

/* One active VT, with a process running on it.
   Kernel index (N in /dev/ttyN and vtx.tty here) is always
   non-zero and may not match indexes in consoles[]. */

struct term {
	int tty;    /* N in ttyN, always 1-based */
	int ttyfd;  /* open fd for the tty above */
	int ctlfd;  /* client control pipe (socket) */
	int pid;
	int graph;
};

/* VT-bound device handle, opened on behalf of vtx.pid and multiplexed
   during VT switches. There's going to be lots of these, like 10 or so
   per vtx. All of them char devices, some DRM and some inputs. */

struct mdev {
	int fd;
	int tty;        /* N in ttyN this fd is associated with */
	uint64_t dev;   /* maj:min, in st_dev encoding */
};

struct conn {
	int fd;
};

struct ucmsg;

extern char** environ;
extern int activetty;
extern int initialtty;
extern int primarytty;
extern int greetertty;
extern int lastusertty;
extern int pollset;
extern int mdevreq;
extern int ctrlfd;
extern int tty0fd;

extern int switchlock;

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

int lock_switch(int* mask);
int unlock_switch(void);
int activate(int tty);
void disable(struct mdev* md, int drop);
int switchto(int tty);

struct term* grab_term_slot(void);
struct conn* grab_conn_slot(void);
struct mdev* grab_mdev_slot(void);
struct term* find_term_by_pid(int pid);
struct term* find_term_by_tty(int tty);
void free_term_slot(struct term* vt);
void free_mdev_slot(struct mdev* md);

int wait_poll(void);

void accept_ctrl(void);
void setup_ctrl(void);
void recv_conn(struct conn* cn);

void recv_pipe(struct term* vt);
void final_enter(struct term* vt);

void clear_ctrl(void);
void poll_inputs(void);
int poll_final(int secs);
void terminate_children(void);
void wait_pids(int shutdown);

void restore_initial_tty(void);
int count_running(void);
void disable_all_devs_for(int tty);

long ioctl(int fd, int req, void* arg, const char* name);
long ioctli(int fd, int req, long arg, const char* name);

int spawn(int tty, char* cmd);
int spawn_pinned(int tty);
int query_empty_tty(void);
int query_greeter_tty(void);
int open_tty_device(int tty);
int show_greeter(void);
void grab_initial_lock(void);

int pinned(int tty);
void scan_pinned(void);
void flush_mdevs(void);

void switch_sigalrm(void);
void switch_sigusr1(void);

void quit(const char* msg, char* arg, int err) noreturn;
