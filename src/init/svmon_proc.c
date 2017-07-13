#include <sys/fork.h>
#include <sys/exec.h>
#include <sys/kill.h>
#include <sys/pipe.h>
#include <sys/fcntl.h>
#include <sys/file.h>
#include <sys/wait.h>
#include <sys/clock.h>

#include <format.h>
#include <string.h>
#include <exit.h>

#include "svmon.h"

static time_t passtime;

static void set_passtime(void)
{
	struct timespec tp = { 0, 0 };
	long ret;

	if((ret = sys_clock_gettime(CLOCK_MONOTONIC, &tp))) {
		report("clock_gettime", "CLOCK_MONOTONIC", ret);
	} else {
		passtime = BOOTCLOCKOFFSET + tp.sec;
	}
}

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
	char* dir = gg.dir;
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

	sys_execve(*argv, argv, gg.env);

	return -1;
}

static void spawn(struct proc* rc)
{
	if(wait_needed(&rc->lastrun, TIME_TO_RESTART))
		return;

	rc->lastsig = 0;

	int pipe[2];
	int pid, ret;

	if((ret = sys_pipe2(pipe, O_NONBLOCK))) {
		report("pipe", NULL, ret);
		return;
	}

	if((pid = sys_fork()) < 0) {
		report("fork", NULL, ret);
		return;
	}

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

	gg.pollset = 0;
}

static void stop(struct proc* rc)
{
	if(rc->flags & P_SIGKILL) {
		/* The process has been sent SIGKILL, still refuses
		   to kick the bucket. Just forget about it then,
		   reset p->pid and let the next initpass restart the entry. */
		if(wait_needed(&rc->lastsig, TIME_TO_SKIP))
			return;
		reprec(rc, "refuses to die on SIGKILL, skipping");
		rc->pid = 0;
	} else if(rc->flags & P_SIGTERM) {
		/* The process has been signalled, but has not died yet */
		if(wait_needed(&rc->lastsig, TIME_TO_SIGKILL))
			return;
		reprec(rc, "refuses to exit, sending SIGKILL");
		sys_kill(rc->pid, SIGKILL);
		rc->flags |= P_SIGKILL;
	} else {
		/* Regular stop() invocation, gently ask the process to leave
		   the kernel process table */

		rc->lastsig = passtime;
		sys_kill(rc->pid, SIGTERM);
		rc->flags |= P_SIGTERM;

		/* Attempt to wake the process up to recieve SIGTERM. */
		/* This must be done *after* sending the killing signal
		   to ensure SIGCONT does not arrive first. */
		if(rc->flags & P_SIGSTOP)
			sys_kill(rc->pid, SIGCONT);

		/* make sure we'll get initpass to send SIGKILL if necessary */
		wait_needed(&rc->lastsig, TIME_TO_SIGKILL);
	}
}

static time_t runtime(struct proc* rc)
{
	if(!passtime)
		set_passtime();

	return passtime - rc->lastrun;
}

static void mark_dead(struct proc* rc, int status)
{
	rc->pid = 0;
	rc->status = status;

	if(runtime(rc) < STABLE_TRESHOLD)
		rc->flags |= P_DISABLED;
	if(rc->flags & P_STALE)
		free_proc_slot(rc);

	/* possibly unexpected death */
	gg.passreq = 1;
}

void waitpids(void)
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

void initpass(void)
{
	struct proc* rc;
	int running = 0;

	set_passtime();

	for(rc = firstrec(); rc; rc = nextrec(rc)) {
		int disabled = (rc->flags & P_DISABLED);

		if(!disabled || rc->pid > 0)
			running = 1;
		if(rc->pid <= 0 && !disabled)
			spawn(rc);
		else if(rc->pid > 0 && disabled)
			stop(rc);
	}

	if(!running)
		gg.reboot = 1;

	passtime = 0;
}

void stop_all_procs(void)
{
	struct proc* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		rc->flags |= P_DISABLED;

	gg.passreq = 1;
}
