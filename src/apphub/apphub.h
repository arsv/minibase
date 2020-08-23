#include <bits/time.h>

#define TM_NONE 0
#define TM_MMAP 1
#define TM_STOP 2

#define NCONNS 32

#define RING_SIZE 8192

struct pollfd;

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

	char** environ;

	int pollset;
	struct pollfd* pfds;
	struct pollfd* pfde;
	int npfds;
	int npsep;

	void* brk;
	void* sep;
	void* ptr;
	void* end;

	struct proc* procs;
	int nprocs;
	int nprocs_nonempty;
	int nprocs_running;

	byte pollbuf[8*8];
	struct conn conns[NCONNS];
	int nconns;
	int nconns_active;

	void* iobuf;
	int iolen;

	int lastxid;
	int timer;
	struct timespec ts;
};

#define CTX struct top* ctx __unused

void check_socket(CTX);

void handle_conn(CTX, struct conn* cn);
void close_conn(CTX, struct conn* cn);

void handle_pipe(CTX, struct proc* pc);
void close_pipe(CTX, struct proc* pc);

int flush_proc(CTX, struct proc* pc);

void maybe_trim_heap(CTX);
void* heap_alloc(CTX, int size);
void maybe_drop_iobuf(CTX);

int spawn_child(CTX, char** argv, char** envp);

void check_children(CTX);
void setup_control(CTX);
