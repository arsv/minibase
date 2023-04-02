#include <sys/file.h>
#include <sys/mman.h>
#include <sys/iovec.h>
#include <sys/signal.h>
#include <sys/creds.h>
#include <sys/fprop.h>
#include <sys/proc.h>

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
		if(!strncmp(pc->name, name, sizeof(pc->name)))
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

static void set_child_output(struct proc* pc, int pipe[2])
{
	sys_close(pipe[0]);

	int fd = pipe[1];

	sys_fcntl3(fd, F_SETFL, 0); /* remove O_NONBLOCK */

	sys_dup2(fd, 0);
	sys_dup2(fd, 1);
	sys_dup2(fd, 2);

	if(fd <= 2) return;

	sys_close(fd);
}

static void set_child_session(struct proc* pc)
{
	struct sigset mask;

	sigemptyset(&mask);

	(void)sys_setsid();
	(void)sys_sigprocmask(SIG_SETMASK, &mask, NULL);
}

static int child(CTX, struct proc* pc, char* path, int pipe[2])
{
	int ret;

	set_child_output(pc, pipe);
	set_child_session(pc);

	char* argv[] = { path, NULL };
	char** envp = ctx->envp;

	ret = sys_execve(path, argv, envp);

	fail(path, NULL, ret);
}

static int spawn_path(CTX, struct proc* pc, char* path)
{
	int ret, pid, pipe[2];

	if((ret = sys_access(path, X_OK)) < 0)
		return ret;

	if((ret = sys_pipe2(pipe, O_NONBLOCK)))
		return ret;

	if((pid = sys_fork()) < 0)
		return ret;
	if(pid == 0)
		_exit(child(ctx, pc, path, pipe));

	pc->pid = pid;
	ctx->nalive++;

	if((ret = sys_close(pipe[1])) < 0)
		fail("close", NULL, ret);

	int fd = pipe[0];

	pc->fd = fd;

	add_proc_fd(ctx, fd, pc);

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

static int restart_proc(CTX, struct proc* pc)
{
	int ret;

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

	return spawn_proc(ctx, pc);
}

static int prep_and_start(CTX, struct proc* pc, char* name)
{
	int len = strlen(name);

	memzero(pc, sizeof(*pc));

	if(len > sizeof(pc->name))
		return -ENAMETOOLONG;

	pc->flags = P_IN_USE;
	pc->fd = -1;

	memcpy(pc->name, name, len);

	return spawn_proc(ctx, pc);
}

/* Proc-related commands */

int start_proc(CTX, char* name)
{
	struct proc* pc;
	int ret;

	if((pc = find_by_name(ctx, name)))
		return restart_proc(ctx, pc);

	if(!(pc = grab_proc_slot(ctx)))
		return -ENOMEM;

	if((ret = prep_and_start(ctx, pc, name)) < 0)
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

int kill_proc(CTX, char* name, int sig)
{
	struct proc* pc;

	if(!(pc = find_by_name(ctx, name)))
		return -ENOENT;

	if(!pc->pid || (pc->flags & P_STATUS))
		return -ESRCH;

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

void proc_died(CTX, struct proc* rc, int status)
{
	int pid = rc->pid;

	ctx->nalive--;

	rc->pid = status;

	if(rc->flags & P_KILLED) {
		rc->flags &= ~P_KILLED;
		rc->pid = 0;
	} else {
		rc->flags |= P_STATUS;
		report_dead(ctx, rc, status);
	}

	close_proc(ctx, rc);

	notify_dead(ctx, pid);
}
