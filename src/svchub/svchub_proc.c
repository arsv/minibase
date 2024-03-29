#include <sys/file.h>
#include <sys/mman.h>
#include <sys/iovec.h>
#include <sys/signal.h>
#include <sys/creds.h>
#include <sys/fprop.h>
#include <sys/proc.h>
#include <sys/time.h>

#include <config.h>
#include <util.h>
#include <string.h>
#include <format.h>
#include <sigset.h>

#include "svchub.h"
#include "common.h"

struct proc* find_by_name(CTX, char* name)
{
	int nprocs = ctx->nprocs;
	struct proc* pc = procs;
	struct proc* pe = procs + nprocs;

	if(!name[0])
		return NULL;

	for(; pc < pe; pc++)
		if(!strcmpn(pc->name, name, sizeof(pc->name)))
			return pc;

	return NULL;
}

static struct proc* grab_proc_slot(CTX)
{
	int nprocs = ctx->nprocs;
	struct proc* pc = procs;
	struct proc* pe = procs + nprocs;

	for(; pc < pe; pc++)
		if(!pc->flags)
			return pc;

	if(nprocs >= NPROCS)
		return NULL;

	ctx->nprocs = nprocs + 1;

	return pc;
}

static void wipe_proc_slot(struct proc* pc)
{
	memzero(pc, sizeof(*pc));
}

static void set_child_output(int* pipe)
{
	if(!pipe) return;

	sys_close(pipe[0]);

	int fd = pipe[1];

	sys_fcntl3(fd, F_SETFL, 0); /* remove O_NONBLOCK */

	sys_dup2(fd, 0);
	sys_dup2(fd, 1);
	sys_dup2(fd, 2);

	if(fd <= 2) return;

	sys_close(fd);
}

static void set_child_session(void)
{
	struct sigset mask;

	sigemptyset(&mask);

	(void)sys_setsid();
	(void)sys_sigprocmask(SIG_SETMASK, &mask, NULL);
}

static int child(CTX, struct proc* pc, char* path, int* pipe)
{
	int ret;

	set_child_output(pipe);
	set_child_session();

	char* argv[] = { path, NULL };
	char** envp = ctx->envp;

	ret = sys_execve(path, argv, envp);

	fail(path, NULL, ret);
}

static void save_child_pipe(CTX, struct proc* pc, int* pipe)
{
	int ret;

	if(!pipe)
		return;

	if((ret = sys_close(pipe[1])) < 0)
		fail("close", NULL, ret);

	int fd = pipe[0];

	pc->fd = fd;

	add_proc_fd(ctx, fd, pc);
}

static int spawn_path(CTX, struct proc* pc, char* path)
{
	int ret, pid, pfds[2];
	int* pipe = pfds;

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;

	if(pc->flags & P_PASS)
		pipe = NULL;
	else if((ret = sys_pipe2(pipe, O_NONBLOCK)))
		return ret;

	if((pid = sys_fork()) < 0)
		return ret;
	if(pid == 0)
		_exit(child(ctx, pc, path, pipe));

	pc->pid = pid;
	ctx->nalive++;

	save_child_pipe(ctx, pc, pipe);

	return 0;
}

static int spawn_proc(CTX, struct proc* pc)
{
	char* dir = INITDIR "/";
	int dlen = strlen(dir);
	int nlen = strnlen(pc->name, sizeof(pc->name));
	int len = dlen + nlen + 1;
	char* path = alloca(len);

	char* p = path;
	char* e = path + len - 1;

	p = fmtstrn(p, e, dir, dlen);
	p = fmtstrn(p, e, pc->name, nlen);

	*p++ = '\0';

	return spawn_path(ctx, pc, path);
}

static void update_proc_time(struct proc* pc)
{
	struct timespec ts;
	int ret;

	if((ret = sys_clock_gettime(CLOCK_MONOTONIC, &ts)) < 0)
		fail("clock_gettime", NULL, ret);

	pc->time = ts.sec;
}

static int ran_long_enough(CTX, struct proc* pc)
{
	uint t0 = pc->time;

	update_proc_time(pc);

	uint t1 = pc->time;

	return (t1 - t0) > 10;
}

static int restart_proc(CTX, struct proc* pc, int flags)
{
	int ret;

	if(flags) /* attempt to restart-and-change-flags */
		return -EINVAL;

	if(pc->flags & P_STATUS) {
		pc->flags &= ~(P_STATUS | P_KILLED);
		pc->pid = 0;
	} else if(pc->pid) {
		return -EBUSY;
	}

	if(!pc->buf)
		;
	else if((ret = flush_ring_buf(pc)) < 0)
		return ret;

	if((ret = spawn_proc(ctx, pc)) < 0)
		return ret;

	update_proc_time(pc);

	return ret;
}

static int prep_and_start(CTX, struct proc* pc, char* name, int flags)
{
	int len = strlen(name);
	int ret;

	memzero(pc, sizeof(*pc));

	if(len > sizeof(pc->name))
		return -ENAMETOOLONG;

	pc->flags = P_IN_USE | flags;
	pc->fd = -1;

	memcpy(pc->name, name, len);

	if((ret = spawn_proc(ctx, pc)) < 0)
		return ret;

	update_proc_time(pc);

	return ret;
}

/* Proc-related commands */

int start_proc(CTX, char* name, int flags)
{
	struct proc* pc;
	int ret;

	if((pc = find_by_name(ctx, name)))
		return restart_proc(ctx, pc, flags);

	if(!(pc = grab_proc_slot(ctx)))
		return -ENOMEM;

	if((ret = prep_and_start(ctx, pc, name, flags)) < 0)
		memzero(pc, sizeof(*pc));

	return ret;
}

int stop_proc(CTX, char* name)
{
	struct proc* pc;
	int ret;

	if(!(pc = find_by_name(ctx, name)))
		return -ENOENT;

	int pid = pc->pid;
	int flags = pc->flags;

	if((flags & P_STATUS) || !pid)
		return -EAGAIN;

	pc->flags = flags | P_KILLED;

	if((ret = sys_kill(pid, SIGCONT)) < 0)
		return ret;
	if((ret = sys_kill(pid, SIGTERM)) < 0)
		return ret;

	return pid;
}

int flush_proc(CTX, char* name)
{
	struct proc* pc;

	if(!(pc = find_by_name(ctx, name)))
		return -ENOENT;

	return flush_ring_buf(pc);
}

int remove_proc(CTX, char* name)
{
	struct proc* pc;
	int ret;

	if(!(pc = find_by_name(ctx, name)))
		return -ENOENT;

	if(pc->pid && !(pc->flags & P_STATUS))
		return -EBUSY;

	if((ret = flush_ring_buf(pc)) < 0)
		return ret;

	wipe_proc_slot(pc);

	return 0;
}

int kill_proc(CTX, char* name, int sig)
{
	struct proc* pc;

	if(!(pc = find_by_name(ctx, name)))
		return -ENOENT;

	if(!pc->pid || (pc->flags & P_STATUS))
		return -EAGAIN;

	return sys_kill(pc->pid, sig);
}

/* Well-behaved services are expected to be silent and/or use syslog
   for logging. However, if a service fails to start, or fails later
   at runtime for some reason, it can and should complain to stderr,
   hopefully indicating the reason it failed. In such cases supervisor
   stores the output and allows the user to review it later to diagnose
   the problem.

   Now generally error messages are small and the most important ones
   are issued last. However, we cannot rely on the whole output of the
   service being small, so the output gets stored in a fixed-size ring
   buffer. And because the service are not expected to output anything
   during normal operation, the buffers are allocated on demand. */

void check_proc(CTX, struct proc* pc)
{
	int ret, fd = pc->fd;
	void* buf = pc->buf;
	int ptr = pc->ptr;
	int size = RINGSIZE;

	if(!buf) {
		int proto = PROT_READ | PROT_WRITE;
		int flags = MAP_PRIVATE | MAP_ANONYMOUS;

		buf = sys_mmap(NULL, size, proto, flags, -1, 0);

		if(mmap_error(buf))
			goto close;

		ptr = 0;
		pc->buf = buf;
		pc->ptr = ptr;
	}

	struct iovec iov[2];
	int off = ptr % size;
	iov[0].base = buf + off;
	iov[0].len = size - off;
	iov[1].base = buf;
	iov[1].len = off;
	int iovcnt = off ? 2 : 1;

	if((ret = sys_readv(fd, iov, iovcnt)) < 0)
		goto close;

	ptr += ret;

	if(ptr > RINGSIZE)
		ptr = RINGSIZE + (ptr % RINGSIZE);

	pc->ptr = ptr;

	return;
close:
	close_proc(ctx, pc);
}

int flush_ring_buf(struct proc* rc)
{
	int ret;
	void* buf = rc->buf;
	int size = RINGSIZE;

	if(!buf)
		return -ENODATA;

	if((ret = sys_munmap(buf, size)) < 0) {
		rc->ptr = 0;
		return ret;
	} else {
		rc->buf = NULL;
		rc->ptr = 0;
	}

	return 0;
}

/* SIGCHLD / proc death handling */

static void report_dead(CTX, struct proc* pc, int status)
{
	int len = 200;
	char* buf = alloca(len);
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");

	p = fmtstrn(p, e, pc->name, sizeof(pc->name));

	if(WIFEXITED(status)) {
		p = fmtstr(p, e, " exit ");
		p = fmtint(p, e, WEXITSTATUS(status));
	} else if(WIFSIGNALED(status)) {
		p = fmtstr(p, e, " signal ");
		p = fmtint(p, e, WTERMSIG(status));
	} else {
		p = fmtstr(p, e, " died");
	}

	*p++ = '\n';

	writeall(STDERR, buf, p - buf);
}

void close_proc(CTX, struct proc* pc)
{
	int ret, fd = pc->fd;

	if(fd < 0)
		return;

	del_epoll_fd(ctx, fd);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);

	pc->fd = -1;
}

static void mark_stopped(CTX, struct proc* pc)
{
	pc->flags &= ~P_KILLED;
	pc->pid = 0;
	pc->time = 0;
}

static void mark_died(CTX, struct proc* pc, int status)
{
	pc->pid = status;
	pc->flags |= P_STATUS;
}

void proc_died(CTX, struct proc* pc, int status)
{
	int pid = pc->pid;
	int ret;

	ctx->nalive--;

	close_proc(ctx, pc);

	if(pc->flags & P_KILLED) {
		mark_stopped(ctx, pc);
		notify_dead(ctx, pid);
		return;
	}

	if((pc->flags & P_ONCE) && !status) {
		flush_ring_buf(pc);
		wipe_proc_slot(pc);
		return;
	}

	report_dead(ctx, pc, status);

	if(pc->flags & P_ONCE)
		;
	else if(!ran_long_enough(ctx, pc))
		warn("not respawning", NULL, 0);
	else if((ret = spawn_proc(ctx, pc)) < 0)
		warn("cannot respawn", NULL, ret);
	else
		return;

	mark_died(ctx, pc, status);
}
