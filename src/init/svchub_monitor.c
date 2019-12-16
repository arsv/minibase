#include <sys/time.h>
#include <sys/signal.h>
#include <sys/file.h>
#include <sys/proc.h>

#include <sigset.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "svchub.h"

static void note_pass_time(CTX)
{
	int ret;
	struct timespec ts;

	if(ctx->timeset)
		return;

	if((ret = sys_clock_gettime(CLOCK_BOOTTIME, &ts)) < 0)
		quit(ctx, "clock_gettime", "BOOTTIME", ret);

	ctx->tm = ts.sec;
	ctx->timeset = 1;
}

static void update_nprocs(CTX)
{
	int n = ctx->nprocs;

	while(n > 0) {
		struct proc* rc = &procs[n-1];

		if(!empty(rc)) continue;

		n--;
	}

	ctx->nprocs = n;
}

void free_proc_slot(CTX, struct proc* rc)
{
	int fd = rc->fd;

	if(rc->buf)
		flush_ring_buf(rc);
	if(fd > 0)
		sys_close(fd);

	rc->fd = -1;
	memzero(rc, sizeof(*rc));
	
	update_nprocs(ctx);
}

static void proc_died(CTX, struct proc* rc, int status)
{
	int flags = rc->flags;
	int pid = rc->pid;

	ctx->active--;

	if(flags & P_STALE) {
		free_proc_slot(ctx, rc);
		return;
	}

	notify_dead(ctx, pid);

	if(flags & P_DISABLE) {
		rc->pid = 0;
		return;
	}

	note_pass_time(ctx);

	rc->pid = (-1 & ~0xFFFF) | (status & 0xFFFF);
	rc->flags |= P_STATUS;

	if(flags & P_RESTART) {
		rc->flags &= ~P_RESTART;
		flush_ring_buf(rc);
	} else if(ctx->tm - rc->tm < STABLE_TRESHOLD) {
		return;
	}

	start_proc(ctx, rc);
}

static struct proc* find_by_pid(CTX, int pid)
{
	struct proc* rc = procs;
	struct proc* re = procs + ctx->nprocs;

	if(pid <= 0)
		;
	else for(; rc < re; rc++)
		if(rc->pid == pid)
			return rc;

	return NULL;
}

void check_children(CTX)
{
	int pid, status;
	struct proc* rc;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		if(!(rc = find_by_pid(ctx, pid)))
			continue; /* some stray child */

		proc_died(ctx, rc, status);
	}

	if(!ctx->active) terminate(ctx);
}

static int child(CTX, struct proc* rc, int pipe[2])
{
	char* dir = INITDIR;
	char* name = rc->name;
	sigset_t mask;

	sigemptyset(&mask);

	FMTBUF(p, e, path, 200);
	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	sys_close(pipe[0]);

	int fd = pipe[1];

	sys_fcntl3(fd, F_SETFL, 0); /* remove O_NONBLOCK */
	sys_dup2(fd, 0);
	sys_dup2(fd, 1);
	sys_dup2(fd, 2);

	if(fd > 2) sys_close(fd);

	sys_sigprocmask(SIG_SETMASK, &mask, NULL);

	char* argv[] = { path, NULL };

	sys_execve(*argv, argv, ctx->environ);

	_exit(-1);
}

static int spawn(CTX, struct proc* rc)
{
	int ret, pid, pipe[2];

	if((ret = sys_pipe2(pipe, O_NONBLOCK)))
		return ret;

	if((pid = sys_fork()) < 0)
		return ret;
	if(pid == 0)
		_exit(child(ctx, rc, pipe));

	note_pass_time(ctx);

	rc->pid = pid;
	sys_close(pipe[1]);
	rc->fd = pipe[0];
	rc->tm = ctx->tm;

	ctx->pollset = 0;
	ctx->active++;

	return 0;
}

int start_proc(CTX, struct proc* rc)
{
	int pid = rc->pid;
	int flags = rc->flags;

	if(pid > 0)
		return -EALREADY;

	rc->flags = flags & ~(P_STATUS | P_DISABLE);

	return spawn(ctx, rc);
}

int stop_proc(CTX, struct proc* rc)
{
	int pid = rc->pid;
	int flags = rc->flags;
	int ret;

	if(pid <= 0) {
		rc->flags &= ~P_STATUS;
		rc->pid = 0;
		return -ESRCH;
	}

	if((ret = sys_kill(pid, SIGCONT)) < 0)
		return ret;

	if((ret = sys_kill(pid, SIGTERM)) < 0)
		return ret;

	rc->flags = flags | P_DISABLE;

	return 0;
}
