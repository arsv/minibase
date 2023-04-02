#include <bits/types.h>
#include <bits/time.h>

#define NAMELEN 16
#define NPROCS 70
#define NCONNS 10

#define RINGSIZE 4096

#define S_UNKNOWN       0
#define S_SYSINIT       1
#define S_STARTUP       2
#define S_RUNNING       3
#define S_STOPPING      4
#define S_SHUTDOWN      5

#define P_IN_USE       (1<<0)
#define P_KILLED       (1<<1)
#define P_STATUS       (1<<2)

#define PKEY(g, k) (((g) << 16) | k)
#define PKEY_GROUP(v) ((v) >> 16)
#define PKEY_INDEX(v) ((v) & 0xFFFF)

struct proc {
	char name[NAMELEN];
	int pid;
	int fd;
	ushort flags;
	ushort ptr;
	void* buf;
};

struct conn {
	int fd;
	int pid;
};

struct top {
	char** argv;
	char** envp;

	int state;
	char* script;
	int scrpid;
	int sigcnt;

	int epfd;
	int ctlfd;
	int sigfd;

	int nconns;
	int nprocs;
	int nalive;
};

#define CTX struct top* ctx __unused

extern struct proc procs[];
extern struct conn conns[];

void start_scripts(CTX);
void check_children(CTX);

void add_sock_fd(CTX, int fd);
void add_conn_fd(CTX, int fd, struct conn* cn);
void add_proc_fd(CTX, int fd, struct proc* pc);
void del_epoll_fd(CTX, int fd);

int command_stop(CTX, char* script);
void signal_stop(CTX, char* script);
void handle_alarm(CTX);

struct proc* find_by_name(CTX, char* name);
//void free_proc_slot(CTX, struct proc* rc);

int start_proc(CTX, char* name);
void proc_died(CTX, struct proc* pc, int status);
int stop_proc(CTX, char* name);
int flush_proc(CTX, char* name);
int kill_proc(CTX, char* name, int sig);

void open_socket(CTX);
void check_socket(CTX);
void check_conn(CTX, struct conn* cn);
void close_conn(CTX, struct conn* cn);
void close_socket(CTX);

void check_proc(CTX, struct proc* rc);
void close_proc(CTX, struct proc* rc);
int flush_ring_buf(struct proc* rc);

void notify_dead(CTX, int pid);
