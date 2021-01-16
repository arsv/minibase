#include <sys/file.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/prctl.h>
#include <sys/signal.h>

#include <string.h>
#include <sigset.h>
#include <format.h>
#include <main.h>
#include <util.h>

ERRTAG("runcg");
ERRLIST(NENOENT NEINVAL NEPERM NEACCES NEFAULT NEISDIR NELOOP NEMFILE NENFILE
        NENOMEM NENOTDIR NECHILD NEAGAIN NENOSYS NEBADF NEINTR NEIO);

struct top {
	int sigfd;
	int grpfd;
	int self;
};

#define CTX struct top* ctx
#define BUF 1024

static int open_tasks(CTX)
{
	int fd, at = ctx->grpfd;

	if((fd = sys_openat(at, "cgroup.procs", O_RDONLY)) >= 0)
		return fd;

	fail("open", "cgroup.procs", fd);
}

static char* parse(char* p, char* e, int* dst)
{
	int pid = 0;

	while(p < e) {
		char c = *p++;

		if(c == '\n') {
			*dst = pid;
			return p;
		} else if(c >= '0' || c <= '9') {
			pid = pid*10 + (c - '0');
		} else {
			break; /* should not happen */
		}
	}

	return NULL;
}

static int read_tasks(int fd, void* buf, int len)
{
	int rd;

	if((rd = sys_read(fd, buf, len)) < 0)
		fail("read", "tasks", rd);

	return rd;
}

static void kill_cgroup(CTX, int sig)
{
	char buf[BUF];
	int rd, pid;
	int fd = open_tasks(ctx);
	int off = 0;
next:
	rd = read_tasks(fd, buf + off, sizeof(buf) - off);

	char* p = buf;
	char* e = buf + rd;
	char* q;

	while((q = parse(p, e, &pid))) {
		if(pid != ctx->self)
			(void)sys_kill(pid, sig);
		p = q;
	}

	long left = e - p;

	if(off + rd < ssizeof(buf)) /* last incomplete chunk */
		goto done;
	if(left < 0 || left > BUF/2) /* something's really off */
		goto done;

	memcpy(buf, p, left);
	off = left;

	goto next;
done:
	sys_close(fd);
}

static void check_initial(CTX)
{
	char buf[BUF];
	int rd, pid;
	int fd = open_tasks(ctx);

	rd = read_tasks(fd, buf, sizeof(buf));

	char* p = buf;
	char* e = buf + rd;

	if(!(p = parse(p, e, &pid)))
		fail("empty group", NULL, 0);
	if(pid != ctx->self)
		fail("non-empty group", NULL, 0);
	if((p = parse(p, e, &pid)))
		fail("non-empty group", NULL, 0);

	sys_close(fd);
}

static void check_remaining(CTX)
{
	char buf[BUF];
	int rd, pid;
	int fd = open_tasks(ctx);

	rd = read_tasks(fd, buf, sizeof(buf));

	char* p = buf;
	char* e = buf + rd;

	if(!(p = parse(p, e, &pid)))
		fail("leaving empty group", NULL, 0);
	if(pid != ctx->self)
		goto out;
	if((p = parse(p, e, &pid)))
		goto out;

	_exit(0x00); /* ctx->self is the only process in the group */
out:
	sys_close(fd);
}

static void handle_sigchld(CTX)
{
	int pid, status;
	int count = 0;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0)
		count++;
	if(pid < 0 && pid != -ECHILD)
		fail("waitpid", NULL, pid);
	if(count)
		check_remaining(ctx);
}

static void monitor_signals(CTX)
{
	int rd, fd = ctx->sigfd;
	struct siginfo si;

	if((rd = sys_read(fd, &si, sizeof(si))) < 0)
		fail("read", "signalfd", rd);

	int sig = si.signo;

	if(sig == SIGCHLD)
		handle_sigchld(ctx);
	else
		kill_cgroup(ctx, sig);
}

static int open_absolute(char* name)
{
	int fd, flags = O_DIRECTORY | O_CLOEXEC;

	if((fd = sys_open(name, flags)) < 0)
		fail(NULL, name, fd);

	return fd;
}

static int open_relative(char* name)
{
	FMTBUF(p, e, path, 256);
	p = fmtstr(p, e, "/sys/fs/cgroup/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	return open_absolute(path);
}

static void query_own_pid(CTX)
{
	int pid;

	if((pid = sys_getpid()) < 0)
		fail("getpid", NULL, pid);

	ctx->self = pid;
}

static void setup_cgroup(CTX, char* name)
{
	int fd;

	if(*name == '/')
		fd = open_absolute(name);
	else
		fd = open_relative(name);

	ctx->grpfd = fd;

	check_initial(ctx);
}

static void setup_signals(CTX)
{
	int ret, fd;
	struct sigset mask;
	int flags = SFD_CLOEXEC;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		fail("signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);
	if((ret = sys_prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0)) < 0)
		fail("prctl", "PR_SET_CHILD_SUBREAPER", ret);

	ctx->sigfd = fd;
}

static void spawn_child(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	struct sigset empty;
	int ret, pid;

	sigemptyset(&empty);

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid > 0) return; /* parent */

	if((ret = sys_sigprocmask(SIG_SETMASK, &empty, NULL)) < 0)
		fail("sigprocmask", NULL, ret);

	ret = sys_execve(*argv, argv, envp);

	fail("execve", *argv, ret);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	if(argc < 3)
		fail("too few arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	query_own_pid(ctx);
	setup_cgroup(ctx, argv[1]);
	setup_signals(ctx);

	spawn_child(argc - 2, argv + 2);

	while(1) monitor_signals(ctx);
}
