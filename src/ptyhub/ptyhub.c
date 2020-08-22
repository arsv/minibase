#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/timer.h>
#include <sys/signal.h>
#include <sys/prctl.h>
#include <sys/mman.h>

#include <string.h>
#include <sigset.h>
#include <printf.h>
#include <main.h>
#include <util.h>

#include "common.h"
#include "ptyhub.h"

ERRTAG("ptyhub");

int extend_heap(CTX, void* to)
{
	void* end = ctx->lastbrk;

	if(to <= end)
		return 0;

	void* new = end + pagealign(to - end);

	end = sys_brk(new);

	if(to > end)
		return -ENOMEM;

	ctx->lastbrk = end;

	return 0;
}

void maybe_trim_heap(CTX)
{
	int nprocs = ctx->nprocs;
	struct proc* procs = ctx->procs;

	void* brk = procs;
	void* ptr = &procs[nprocs];
	void* end = ctx->lastbrk;

	void* new = brk + pagealign(ptr - brk);

	if(new >= end)
		return;

	end = sys_brk(new);

	ctx->lastbrk = end;
}

static void kill_all_procs(CTX)
{
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;
	int pid;

	for(; pc < pe; pc++) {
		if((pid = pc->pid) <= 0)
			continue;

		(void)sys_kill(pid, SIGTERM);
	}
}

static void kill_all_conns(CTX)
{
	struct conn* cn = ctx->conns;
	struct conn* ce = cn + ctx->nconns;
	int fd;

	for(; cn < ce; cn++) {
		if((fd = cn->fd) <= 0)
			continue;

		sys_close(fd);
		cn->fd = -1;
	}

	sys_close(ctx->ctlfd);
	ctx->ctlfd = -1;
}

static void sigprocmask(int how, struct sigset* mask)
{
	int ret;

	if((ret = sys_sigprocmask(how, mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);
}

static void open_signalfd(CTX)
{
	int fd;
	struct sigset ss;

	sigemptyset(&ss);

	sigaddset(&ss, SIGINT);
	sigaddset(&ss, SIGHUP);
	sigaddset(&ss, SIGALRM);
	sigaddset(&ss, SIGTERM);
	sigaddset(&ss, SIGCHLD);

	sigprocmask(SIG_BLOCK, &ss);

	if((fd = sys_signalfd(-1, &ss, SFD_CLOEXEC)) < 0)
		fail("signalfd", NULL, fd);

	ctx->sigfd = fd;
}

static void set_timer(CTX, int type, int sec)
{
	struct itimerval itv;
	int ret;

	memzero(&itv, sizeof(itv));

	itv.value.sec = sec;

	if((ret = sys_setitimer(ITIMER_REAL, &itv, NULL)) < 0)
		warn("setitimer", NULL, ret);
	else
		ctx->timer = type;
}

void set_iobuf_timer(CTX)
{
	if(ctx->timer == TM_STOP)
		return;

	set_timer(ctx, TM_MMAP, 10);
}

static void clear_exit(CTX)
{
	_exit(0xFF);
}

static void maybe_normal_exit(CTX)
{
	if(ctx->timer != TM_STOP)
		return;
	if(ctx->nprocs_running)
		return;

	clear_exit(ctx);
}

static void request_stop(CTX, const char* signame)
{
	if(ctx->timer == TM_STOP) {
		warn(signame, NULL, 0);
		clear_exit(ctx);
	}

	kill_all_procs(ctx);
	kill_all_conns(ctx);

	set_timer(ctx, TM_STOP, 5);

	maybe_normal_exit(ctx);
}

static void handle_timeout(CTX)
{
	int timer = ctx->timer;

	ctx->timer = TM_NONE;

	if(timer == TM_MMAP)
		maybe_drop_iobuf(ctx);
	if(timer == TM_STOP)
		clear_exit(ctx);
}

static void check_signal(CTX)
{
	struct siginfo si;
	int fd = ctx->sigfd;
	int ret;

	memzero(&si, sizeof(si));

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		fail("read", "sigfd", ret);
	if(ret == 0)
		fail("signalfd EOF", NULL, 0);

	int sig = si.signo;

	if(sig == SIGCHLD) {
		check_children(ctx);
		maybe_normal_exit(ctx);
	} else if(sig == SIGINT) {
		request_stop(ctx, "SIGINT");
	} else if(sig == SIGTERM) {
		request_stop(ctx, "SIGTERM");
	} else if(sig == SIGHUP) {
		request_stop(ctx, "SIGHUP");
	} else if(sig == SIGALRM) {
		handle_timeout(ctx);
	}
}

static void add_epoll_fd(CTX, int fd, int key)
{
	int ret;

	if(PKEY_INDEX(key) == PKEY_INDEX(-1)) {
		tracef("skipping invalid key\n");
		return;
	}

	int epfd = ctx->epfd;
	struct epoll_event ev;

	memzero(&ev, sizeof(ev));

	ev.events = EPOLLIN;
	ev.data.fd = key;

	if((ret = sys_epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) < 0)
		warn("epoll_ctl", NULL, ret);
}

static void del_epoll_fd(CTX, int fd)
{
	int ret;
	int epfd = ctx->epfd;

	if((ret = sys_epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL)) < 0)
		warn("epoll_del", NULL, ret);
}

static int index_of(CTX, void* ptr, int size, void* ref)
{
	if(ptr < ref) {
		warn("ref under base", NULL, 0);
		return -1;
	}

	long d = ptr - ref;

	if(d % size) {
		warn("ref misaligned", NULL, 0);
		return -1;
	}

	return (d / size);
}

static int conn_index(CTX, struct conn* cn)
{
	return index_of(ctx, cn, sizeof(*cn), ctx->conns);
}

static int proc_index(CTX, struct proc* pc)
{
	return index_of(ctx, pc, sizeof(*pc), ctx->procs);
}

void add_conn_fd(CTX, struct conn* cn)
{
	int idx = conn_index(ctx, cn);

	add_epoll_fd(ctx, cn->fd, PKEY(1, idx));
}

void del_conn_fd(CTX, struct conn* cn)
{
	del_epoll_fd(ctx, cn->fd);
}

void add_stdout_fd(CTX, struct proc* pc)
{
	int idx = proc_index(ctx, pc);

	add_epoll_fd(ctx, pc->mfd, PKEY(2, idx));
}

void add_stderr_fd(CTX, struct proc* pc)
{
	int idx = proc_index(ctx, pc);

	add_epoll_fd(ctx, pc->efd, PKEY(3, idx));
}

void del_stdout_fd(CTX, struct proc* pc)
{
	del_epoll_fd(ctx, pc->mfd);
}

void del_stderr_fd(CTX, struct proc* pc)
{
	del_epoll_fd(ctx, pc->efd);
}

static struct proc* get_proc_by(CTX, int idx)
{
	int nprocs = ctx->nprocs;

	if(idx >= nprocs)
		fail("epoll proc idx out of range", NULL, 0);

	struct proc* procs = ctx->procs;

	return &procs[idx];
}

static void process_stdout(CTX, int idx, int events)
{
	struct proc* pc = get_proc_by(ctx, idx);

	if(events & EPOLLIN)
		handle_stdout(ctx, pc);
	if(events & ~EPOLLIN)
		close_stdout(ctx, pc);
}

static void process_stderr(CTX, int idx, int events)
{
	struct proc* pc = get_proc_by(ctx, idx);

	if(events & EPOLLIN)
		handle_stderr(ctx, pc);
	if(events & ~EPOLLIN)
		close_stderr(ctx, pc);
}

static void process_conn(CTX, int idx, int events)
{
	int nconns = ctx->nconns;

	if(idx >= nconns)
		fail("epoll conn idx out of range", NULL, 0);

	struct conn* conns = ctx->conns;
	struct conn* cn = &conns[idx];

	if(events & EPOLLIN)
		handle_conn(ctx, cn);
	if(events & ~EPOLLIN)
		close_conn(ctx, cn);
}

static void process_ctlfd(CTX, int events)
{
	if(events & EPOLLIN)
		return check_socket(ctx);
	if(events & ~EPOLLIN)
		fail("controlfd error", NULL, 0);
}

static void process_sigfd(CTX, int events)
{
	if(events & EPOLLIN)
		return check_signal(ctx);
	if(events & ~EPOLLIN)
		fail("signalfd error", NULL, 0);
}

static void process_misc(CTX, int idx, int events)
{
	if(idx == 1)
		return process_ctlfd(ctx, events);
	if(idx == 2)
		return process_sigfd(ctx, events);

	fail("unexpected epoll event key", NULL, idx);
}

static void process_event(CTX, int key, int events)
{
	int group = PKEY_GROUP(key);
	int idx = PKEY_INDEX(key);

	if(group == 0)
		return process_misc(ctx, idx, events);
	if(group == 1)
		return process_conn(ctx, idx, events);
	if(group == 2)
		return process_stdout(ctx, idx, events);
	if(group == 3)
		return process_stderr(ctx, idx, events);

	fail("unexpected epoll event group", NULL, group);
}

static void poll(CTX)
{
	int ret;
	struct epoll_event ev;

	if((ret = sys_epoll_wait(ctx->epfd, &ev, 1, -1)) < 0)
		fail("epoll_wait", NULL, ret);

	process_event(ctx, ev.data.fd, ev.events);
}

static void add_epoll_static(int epfd, int key, int fd)
{
	struct epoll_event ev;
	int ret;

	memzero(&ev, sizeof(ev));

	ev.events = EPOLLIN;
	ev.data.fd = key;

	if((ret = sys_epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) < 0)
		fail("epoll_ctl", NULL, ret);
}

static void prepare_epoll(CTX)
{
	int fd;

	if((fd = sys_epoll_create()) < 0)
		fail("epoll_create", NULL, fd);

	ctx->epfd = fd;

	add_epoll_static(fd, PKEY(0, 1), ctx->ctlfd);
	add_epoll_static(fd, PKEY(0, 2), ctx->sigfd);
}

static void init_heap_ptr(CTX)
{
	void* brk = sys_brk(NULL);

	ctx->procs = brk;
	ctx->lastbrk = brk;
}

static void set_subreaper(void)
{
	int ret;

	if((ret = sys_prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0)) < 0)
		fail("prctl", "PR_SET_CHILD_SUBREAPER", ret);
}

int main(int argc, char** argv)
{
	(void)argv;
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	set_subreaper();
	init_heap_ptr(ctx);

	open_signalfd(ctx);
	setup_control(ctx);
	prepare_epoll(ctx);

	while(1) poll(ctx);
}
