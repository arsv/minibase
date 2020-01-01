#include <bits/types.h>
#include <bits/time.h>

#define NAMELEN 15
#define NPROCS 70
#define NCONNS 10
#define NPFDS (2+NCONNS+NPROCS)

#define STABLE_TRESHOLD 30

#define RINGSIZE 4096

#define P_DISABLE       (1<<0)
#define P_RESTART       (1<<1)
#define P_STALE         (1<<2)
#define P_STATUS        (1<<3)

struct proc {
	char name[NAMELEN];
	byte flags;
	time_t tm;
	int pid;
	int fd;
	void* buf;
	int ptr;
};

struct conn {
	int fd;
	int pid;
};

struct top {
	char** environ;
	time_t passtime;
	char* reboot;
	char* rbscript;

	int ctlfd;
	int sigfd;

	int pollset;
	int timeset;
	int active;
	int sigcnt;

	time_t tm;

	int nprocs;
	int nconns;
	int npfds;
};

#define CTX struct top* ctx __unused

extern struct proc procs[];
extern struct conn conns[];

void noreturn quit(CTX, const char* msg, char* arg, int err);
int stop_into(CTX, const char* script);
void signal_stop(CTX, const char* script);

struct proc* find_by_name(CTX, char* name);
void free_proc_slot(CTX, struct proc* rc);

int reload_procs(CTX);
void reassess_procs(CTX);
void check_children(CTX);

int start_proc(CTX, struct proc* rc);
int stop_proc(CTX, struct proc* rc);

void setup_control(CTX);
void check_control(CTX);
void check_conn(CTX, struct conn* cn);
void close_conn(CTX, struct conn* cn);

void check_proc(CTX, struct proc* rc);
void close_proc(CTX, struct proc* rc);
int flush_ring_buf(struct proc* rc);

void notify_dead(CTX, int pid);

void terminate(CTX);

static inline int empty(struct proc* pc) { return !pc->name[0]; }
