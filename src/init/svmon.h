#include <bits/types.h>
#include "config.h"

#define P_DISABLED      (1<<0)
#define P_SIGSTOP       (1<<1)
#define P_SIGTERM       (1<<2)
#define P_SIGKILL       (1<<3)
#define P_STALE         (1<<4)

#define F_WAIT_PIDS     (1<<0)
#define F_SETUP_CTRL    (1<<1)
#define F_RELOAD_PROCS  (1<<2)
#define F_CHECK_PROCS   (1<<3)
#define F_FLUSH_HEAP    (1<<4)
#define F_TRIM_RING     (1<<5)
#define F_UPDATE_PFDS   (1<<6)

struct proc {
	int pid;
	uint8_t flags;
	char name[NAMELEN];
	time_t lastrun;
	time_t lastsig;
	int status;
	int pipefd;
	/* ring buffer */
	short idx;
	short ptr;
};

struct conn {
	int fd;
};

struct ucmsg;

extern char rbcode;
extern int ctrlfd;
extern char* confdir;
extern char** environ;
extern time_t passtime;

extern struct proc procs[];
extern struct conn conns[];
extern int nprocs;
extern int nconns;

struct proc* firstrec(void);
struct proc* nextrec(struct proc* rc);

struct proc* find_by_name(char* name);
struct proc* find_by_pid(int pid);
struct proc* grab_proc_slot(void);
void free_proc_slot(struct proc* rc);

struct conn* grab_conn_slot(void);
void free_conn_slot(struct conn* cn);

void set_passtime(void);
int runtime(struct proc* rc);
void check_procs(void);
void update_poll_fds(void);
void wait_poll(void);
void wait_pids(void);

int setup_signals(void);
void setup_ctrl(void);
void accept_ctrl(int sfd);
void handle_conn(struct conn* cn);
void wakeupin(int ttw);
void stop_all_procs(void);
int dispatch_cmd(struct conn* cn, struct ucmsg* msg);
void request(int flag);

char* ring_buf_for(struct proc* rc);
int read_into_ring_buf(struct proc* rc, int fd);
void flush_ring_buf(struct proc* rc);
void trim_ring_area(void);

int reload_procs(void);

void report(char* msg, char* arg, int err);
void reprec(struct proc* rc, char* msg);

void setup_heap(void);
void* heap_alloc(int len);
void trim_heap(void* ptr);
void flush_heap(void);
