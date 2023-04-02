#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/signal.h>
#include <sys/epoll.h>

#include <main.h>
#include <sigset.h>
#include <printf.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "svchub.h"

ERRTAG("svchub");

struct proc procs[NPROCS];
struct conn conns[NCONNS];

static void check_signal(CTX)
{
	struct siginfo si;
	int ret, fd = ctx->sigfd;

	memzero(&si, sizeof(si));

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		fail("read", "signalfd", ret);
	else if(ret == 0)
		fail("signalfd", "EOF", 0);

	int sig = si.signo;

	if(sig == SIGCHLD)
		check_children(ctx);
	else if(sig == SIGPWR)
		signal_stop(ctx, "poweroff");
	else if(sig == SIGINT)
		signal_stop(ctx, "reboot");
	else if(sig == SIGTERM)
		signal_stop(ctx, "reboot");
	else if(sig == SIGALRM)
		handle_alarm(ctx);
	else
		fail("signal", NULL, sig);
}

static void process_sigfd(CTX, int events)
{
	if(events & EPOLLIN)
		check_signal(ctx);
	if(events & ~EPOLLIN)
		fail("signalfd", "lost", 0);
}

static void process_ctlfd(CTX, int events)
{
	if(events & EPOLLIN)
		check_socket(ctx);
	if(events & ~EPOLLIN)
		fail("control", "lost", 0);
}

static void process_proc(CTX, int idx, int events)
{
	int nprocs = ctx->nprocs;

	if(idx >= nprocs)
		fail("epoll proc idx out of range", NULL, 0);

	struct proc* pc = &procs[idx];

	if(events & EPOLLIN)
		check_proc(ctx, pc);
	if(events & ~EPOLLIN)
		close_proc(ctx, pc);
}

static void process_conn(CTX, int idx, int events)
{
	int nconns = ctx->nconns;

	if(idx >= nconns)
		fail("epoll conn idx out of range", NULL, 0);

	struct conn* cn = &conns[idx];

	if(events & EPOLLIN)
		check_conn(ctx, cn);
	if(events & ~EPOLLIN)
		close_conn(ctx, cn);
}

static int add_epoll_fd(CTX, int fd, int key)
{
	int ret, epfd = ctx->epfd;
	struct epoll_event ev;

	memzero(&ev, sizeof(ev));

	ev.events = EPOLLIN;
	ev.data.fd = key;

	if((ret = sys_epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev)) < 0)
		warn("epoll_ctl", NULL, ret);

	return ret;
}

void del_epoll_fd(CTX, int fd)
{
	int ret;
	int epfd = ctx->epfd;

	if((ret = sys_epoll_ctl(epfd, EPOLL_CTL_DEL, fd, NULL)) >= 0)
		return;
	if(ret == -ENOENT)
		return;

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
	return index_of(ctx, cn, sizeof(*cn), conns);
}

static int proc_index(CTX, struct proc* pc)
{
	return index_of(ctx, pc, sizeof(*pc), procs);
}

void add_sock_fd(CTX, int fd)
{
	add_epoll_fd(ctx, fd, PKEY(0, 2));
}

void add_conn_fd(CTX, int fd, struct conn* cn)
{
	int idx = conn_index(ctx, cn);

	add_epoll_fd(ctx, fd, PKEY(1, idx));
}

void add_proc_fd(CTX, int fd, struct proc* pc)
{
	int idx = proc_index(ctx, pc);

	add_epoll_fd(ctx, fd, PKEY(2, idx));
}

static void process_misc(CTX, int idx, int events)
{
	if(idx == 1)
		return process_sigfd(ctx, events);
	if(idx == 2)
		return process_ctlfd(ctx, events);

	fail("unexpected epoll event key", NULL, idx);
}

static void wait_poll(CTX)
{
	int ret, fd = ctx->epfd;
	struct epoll_event ep;

	if((ret = sys_epoll_wait(fd, &ep, 1, -1)) < 0)
		fail("epoll", NULL, ret);

	int key = ep.data.fd;
	int events = ep.events;

	int group = PKEY_GROUP(key);
	int idx = PKEY_INDEX(key);

	if(group == 0)
		process_misc(ctx, idx, events);
	else if(group == 1)
		process_conn(ctx, idx, events);
	else if(group == 2)
		process_proc(ctx, idx, events);
	else
		fail("unexpected epoll group", NULL, idx);
}

static void setup_signals(CTX)
{
	int fd, ret;
	int flags = SFD_NONBLOCK | SFD_CLOEXEC;
	struct sigset mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGPWR);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGCHLD);
	sigaddset(&mask, SIGALRM);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		fail("signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);

	ctx->sigfd = fd;

	add_epoll_fd(ctx, fd, PKEY(0, 1));
}

static void prepare_epoll(CTX)
{
	int fd;

	if((fd = sys_epoll_create1(O_CLOEXEC)) < 0)
		fail("epoll_create", NULL, fd);

	ctx->epfd = fd;
}

static void setup_std_fds(CTX)
{
	int fd;

	if(sys_fcntl(STDERR, F_GETFD) >= 0)
		return; /* if 2 is ok, then 0 and 1 must be valid as well */

	if((fd = sys_open("/dev/null", O_RDWR)) >= 0)
		goto gotfd;
	if((fd = sys_open("/", O_PATH)) >= 0)
		goto gotfd;
gotfd:
	if((fd < 1) && (sys_dup2(fd, STDOUT) < 0))
		goto panic; /* cannot set stdout */
	if((fd < 2) && (sys_dup2(fd, STDERR) < 0))
		goto panic; /* cannot set stderr */
	if(fd <= 2 || (sys_close(fd) >= 0))
		return;
panic:
	_exit(0xFF);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->argv = argv;
	ctx->envp = argv + argc + 1;

	setup_std_fds(ctx);

	prepare_epoll(ctx);
	setup_signals(ctx);

	start_scripts(ctx);

	while(1) wait_poll(ctx);
}
