#include <bits/time.h>

#define TM_NONE 0
#define TM_MMAP 1
#define TM_STOP 2

#define NCONNS 32

#define RING_SIZE 8192

#define PKEY(g, k) (((g) << 16) | k)
#define PKEY_GROUP(v) ((v) >> 16)
#define PKEY_INDEX(v) ((v) & 0xFFFF)

struct conn {
	int fd;
};

struct proc {
	int xid;
	int pid;
	int fd;
	char name[20];
	void* buf;
	int ptr;
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

	void* iobuf;
	int iolen;

	int lastxid;
	int timer;

	void* lastbrk;
	struct proc* procs;
	struct conn conns[NCONNS];
};

#define CTX struct top* ctx __unused

void check_socket(CTX);

void handle_conn(CTX, struct conn* cn);
void close_conn(CTX, struct conn* cn);

void handle_pipe(CTX, struct proc* pc);
void close_pipe(CTX, struct proc* pc);

int flush_proc(CTX, struct proc* pc);

void maybe_trim_heap(CTX);
void maybe_drop_iobuf(CTX);
int extend_heap(CTX, void* to);

int spawn_child(CTX, char** argv, char** envp);

void check_children(CTX);
void setup_control(CTX);

void add_conn_fd(CTX, int fd, struct conn* cn);
void add_pipe_fd(CTX, int fd, struct proc* pc);
void del_poll_fd(CTX, int fd);

void set_iobuf_timer(CTX);
