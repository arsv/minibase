#include <bits/time.h>

#define TM_NONE 0
#define TM_MMAP 1
#define TM_STOP 2

#define NCONNS 32

#define RING_SIZE 8192

#define PKEY(g, k) (((g) << 16) | k)
#define PKEY_GROUP(v) ((v) >> 16)
#define PKEY_INDEX(v) ((v) & 0xFFFF)

struct pollfd;

struct conn {
	int fd;
};

struct proc {
	int xid;
	int pid; /* child process */
	int mfd; /* pty master side */
	int cfd; /* client connection */
	int efd; /* child stderr */
	int ptr;
	void* buf;
	char name[20];
};

struct top {
	int ctlfd;
	int sigfd;
	int epfd;

	char** environ;

	int nprocs;
	int nprocs_nonempty;
	int nprocs_running;

	int nconns;
	int nconns_active;

	int timer;
	int lastxid;

	void* iobuf;
	int iolen;

	void* lastbrk;
	struct proc* procs;
	struct conn conns[NCONNS];
};

#define CTX struct top* ctx __unused

void check_socket(CTX);

void handle_conn(CTX, struct conn* cn);
void close_conn(CTX, struct conn* cn);

void handle_stdout(CTX, struct proc* pc);
void close_stdout(CTX, struct proc* pc);

void handle_stderr(CTX, struct proc* pc);
void close_stderr(CTX, struct proc* pc);

int flush_proc(CTX, struct proc* pc);
int flush_dead_procs(CTX);

void maybe_trim_heap(CTX);
struct proc* alloc_proc(CTX);
void maybe_drop_iobuf(CTX);

int spawn_child(CTX, char** argv, char** envp);

void check_children(CTX);

void notify_exit(CTX, struct proc* pc, int status);
void setup_control(CTX);

void add_stdout_fd(CTX, struct proc* pc);
void del_stdout_fd(CTX, struct proc* pc);
void add_stderr_fd(CTX, struct proc* pc);
void del_stderr_fd(CTX, struct proc* pc);

void add_conn_fd(CTX, struct conn* cn);
void del_conn_fd(CTX, struct conn* cn);

int extend_heap(CTX, void* to);

void set_iobuf_timer(CTX);
