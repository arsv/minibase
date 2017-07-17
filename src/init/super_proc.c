#include <sys/fork.h>
#include <sys/exec.h>
#include <sys/kill.h>
#include <sys/pipe.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>

#include <format.h>
#include <string.h>
#include <exit.h>

#include "super.h"

static int wait_needed(time_t* last, time_t wait)
{
	time_t curtime = passtime;
	time_t endtime = *last + wait;

	if(endtime <= curtime) {
		*last = passtime;
		return 0;
	} else {
		wakeupin(endtime - curtime);
		return 1;
	}
}

static int child(struct proc* rc)
{
	char* dir = confdir;
	int dlen = strlen(dir);
	char* base = rc->name;
	char blen = strlen(base);

	char path[dlen+blen+2];
	char* p = path;
	char* e = path + sizeof(path) - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, base);
	*p = '\0';

	char* argv[] = { path, NULL };

	sys_execve(*argv, argv, environ);

	return -1;
}

static void spawn(struct proc* rc)
{
	if(wait_needed(&rc->lastrun, TIME_TO_RESTART))
		return;

	rc->lastsig = 0;

	int pipe[2];
	int pid, ret;

	if((ret = sys_pipe2(pipe, O_NONBLOCK)))
		return report("pipe", NULL, ret);

	if((pid = sys_fork()) < 0)
		return report("fork", NULL, ret);

	if(pid == 0) {
		sys_close(pipe[0]);
		sys_dup2(pipe[1], 1);
		sys_dup2(pipe[1], 2);
		_exit(child(rc));
	} else {
		rc->pid = pid;
		sys_close(pipe[1]);
		rc->pipefd = pipe[0];
	}

	request(F_UPDATE_PFDS);
}

static void mark_dead(struct proc* rc, int status)
{
	rc->pid = 0;
	rc->status = status;
	rc->flags &= ~(P_SIGTERM | P_SIGKILL | P_SIGSTOP);

	if(rc->flags & P_RESTART)
		rc->flags &= ~P_RESTART;
	else if(rc->flags & (P_DISABLED | P_STALE))
		;
	else if(!status) /* successful exits are ok */
		;
	else if(runtime(rc) < STABLE_TRESHOLD)
		rc->flags |= P_FAILED;

	if(rc->flags & (P_STALE | P_DISABLED))
		flush_ring_buf(rc);
	if(rc->flags & P_STALE)
		free_proc_slot(rc);

	request(F_CHECK_PROCS);
}

static void stop(struct proc* rc)
{
	if(rc->flags & P_STUCK) {
		return;
	} else if(rc->flags & P_SIGKILL) {
		if(wait_needed(&rc->lastsig, TIME_TO_SKIP))
			return;
		if(rbcode) {
			reprec(rc, "refuses to die, skipping");
			mark_dead(rc, -1);
		} else {
			reprec(rc, "refuses to die on SIGKILL");
			rc->flags |= P_STUCK;
		}
	} else if(rc->flags & P_SIGTERM) {
		if(wait_needed(&rc->lastsig, TIME_TO_SIGKILL))
			return;
		reprec(rc, "refuses to exit, sending SIGKILL");
		rc->lastsig = passtime;
		sys_kill(rc->pid, SIGKILL);
		rc->flags |= P_SIGKILL;
	} else {
		rc->lastsig = passtime;
		sys_kill(rc->pid, SIGTERM);
		rc->flags |= P_SIGTERM;

		if(rc->flags & P_SIGSTOP)
			sys_kill(rc->pid, SIGCONT);

		wait_needed(&rc->lastsig, TIME_TO_SIGKILL);
	}
}

int runtime(struct proc* rc)
{
	if(!passtime)
		set_passtime();

	time_t diff = passtime - rc->lastrun;

	if(diff < 0)
		return 0;
	if(diff > 0x7FFFFFFF)
		return 0x7FFFFFFF;

	return diff;
}

void wait_pids(void)
{
	pid_t pid;
	int status;
	struct proc *rc;
	const int flags = WNOHANG | WUNTRACED | WCONTINUED;

	while((pid = sys_waitpid(-1, &status, flags)) > 0) {
		if(!(rc = find_by_pid(pid)))
			continue; /* Some stray child died. Like we care. */
		if(WIFSTOPPED(status))
			rc->flags |= P_SIGSTOP;
		else if(WIFCONTINUED(status))
			rc->flags &= ~P_SIGSTOP;
		else
			mark_dead(rc, status);
	}

	passtime = 0;
}

void check_procs(void)
{
	struct proc* rc;
	int running = 0;

	set_passtime();

	for(rc = firstrec(); rc; rc = nextrec(rc)) {
		int disabled = (rc->flags & (P_DISABLED | P_FAILED));

		if(!disabled || rc->pid > 0)
			running = 1;
		if(rc->pid <= 0 && !disabled)
			spawn(rc);
		else if(rc->pid > 0 && disabled)
			stop(rc);
	}

	if(!running && !rbcode)
		rbcode = 'r';
}

void stop_all_procs(void)
{
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		rc->flags |= P_DISABLED;

	request(F_CHECK_PROCS);
}
