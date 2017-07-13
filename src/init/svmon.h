#include <bits/types.h>
#include "config.h"

#define P_DISABLED (1<<0)
#define P_SIGSTOP  (1<<1)
#define P_SIGTERM  (1<<2)
#define P_SIGKILL  (1<<3)
#define P_STALE    (1<<4)

struct top {
	int uid;
	int outfd;
	int ctrlfd;
	char rbcode;

	short reopen;
	short reload;
	short reboot;
	short pollset;
	short passreq;
	short termreq;
	short sigchld;
	short heapreq;

	char* dir;
	char** env;

	int nr;
};

struct proc {
	int pid;
	uint8_t flags;
	char name[NAMELEN];
	time_t lastrun;
	time_t lastsig;
	int status;
	int pipefd;
};

struct ring {
	char* buf;
	short ptr;
};

struct conn {
	int fd;
};

struct ucmsg;

extern struct top gg;
extern struct proc procs[];
extern struct conn conns[];
extern int nprocs;
extern int nconns;

int reload_procs(void);

struct proc* find_by_name(char* name);
struct proc* find_by_pid(int pid);
struct proc* grab_proc_slot(void);
void free_proc_slot(struct proc* rc);

struct conn* grab_conn_slot(void);
void free_conn_slot(struct conn* cn);

void initpass(void);
void waitpoll(void);
void waitpids(void);

int setup_signals(void);
void setup_ctrl(void);
void accept_ctrl(int sfd);
void handle_conn(struct conn* cn);
void close_conn(struct conn* cn);
void wakeupin(int ttw);
void stop_all_procs(void);
int dispatch_cmd(struct conn* cn, struct ucmsg* msg);

void set_ctrl_fd(int fd);
void set_proc_fd(struct proc* rc, int fd);
void set_conn_fd(struct conn* cn, int fd);

void flush_ring_buf(struct proc* rc);
struct ring* ring_buf_for(struct proc* rc);

struct proc* firstrec(void);
struct proc* nextrec(struct proc* rc);
int proc_index(struct proc* rc);
int conn_index(struct conn* cn);

void report(char* msg, char* arg, int err);
void reprec(struct proc* rc, char* msg);

void setup_heap(void);
void* heap_alloc(int len);
void heap_trim(void* ptr);
void heap_flush(void);
