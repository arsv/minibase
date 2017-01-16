#include <sys/fork.h>
#include <sys/execve.h>
#include <sys/kill.h>
#include <sys/pipe2.h>
#include <sys/close.h>
#include <sys/dup2.h>
#include <sys/waitpid.h>
#include <sys/clock_gettime.h>
#include <sys/_exit.h>

#include <format.h>
#include <string.h>

#include "svcmon.h"

static time_t passtime;

static void setpasstime(void)
{
	struct timespec tp = { 0, 0 };
	long ret;

	if((ret = sysclock_gettime(CLOCK_MONOTONIC, &tp))) {
		report("clock_gettime", "CLOCK_MONOTONIC", ret);
	} else {
		passtime = BOOTCLOCKOFFSET + tp.tv_sec;
	}
}

static int waitneeded(time_t* last, time_t wait)
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

static int child(struct svcrec* rc)
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

	sysexecve(*argv, argv, gg.env);

	return -1;
}

static void spawn(struct svcrec* rc)
{
	if(waitneeded(&rc->lastrun, TIME_TO_RESTART))
		return;

	rc->lastrun = passtime;
	rc->lastsig = 0;

	int pipe[2];
	int pid, ret;

	if((ret = syspipe2(pipe, O_NONBLOCK))) {
		report("pipe", NULL, ret);
		return;
	}

	if((pid = sysfork()) < 0) {
		report("fork", NULL, ret);
		return;
	}

	if(pid == 0) {
		sysclose(pipe[0]);
		sysdup2(pipe[1], 1);
		sysdup2(pipe[1], 2);
		_exit(child(rc));
	} else {
		rc->pid = pid;
		sysclose(pipe[1]);
		setpollfd(rc, pipe[0]);
	}
}

static void stop(struct svcrec* rc)
{
	if(rc->flags & P_SIGKILL) {
		/* The process has been sent SIGKILL, still refuses
		   to kick the bucket. Just forget about it then,
		   reset p->pid and let the next initpass restart the entry. */
		if(waitneeded(&rc->lastsig, TIME_TO_SKIP))
			return;
		reprec(rc, "refuses to die on SIGKILL, skipping");
		rc->pid = 0;
	} else if(rc->flags & P_SIGTERM) {
		/* The process has been signalled, but has not died yet */
		if(waitneeded(&rc->lastsig, TIME_TO_SIGKILL))
			return;
		reprec(rc, "refuses to exit, sending SIGKILL");
		syskill(rc->pid, SIGKILL);
		rc->flags |= P_SIGKILL;
	} else {
		/* Regular stop() invocation, gently ask the process to leave
		   the kernel process table */

		rc->lastsig = passtime;
		syskill(rc->pid, SIGTERM);
		rc->flags |= P_SIGTERM;

		/* Attempt to wake the process up to recieve SIGTERM. */
		/* This must be done *after* sending the killing signal
		   to ensure SIGCONT does not arrive first. */
		if(rc->flags & P_SIGSTOP)
			syskill(rc->pid, SIGCONT);

		/* make sure we'll get initpass to send SIGKILL if necessary */
		waitneeded(&rc->lastsig, TIME_TO_SIGKILL);
	}
}

static void markdead(struct svcrec* rc, int status)
{
	rc->pid = 0;
	rc->status = status;

	if(rc->flags & P_STALE)
		droprec(rc);

	/* possibly unexpected death */
	gg.state |= S_PASSREQ;
}

void waitpids(void)
{
	pid_t pid;
	int status;
	struct svcrec *rc;
	const int flags = WNOHANG | WUNTRACED | WCONTINUED;

	while((pid = syswaitpid(-1, &status, flags)) > 0) {
		if(!(rc = findpid(pid)))
			continue; /* Some stray child died. Like we care. */
		if(WIFSTOPPED(status))
			rc->flags |= P_SIGSTOP;
		else if(WIFCONTINUED(status))
			rc->flags &= ~P_SIGSTOP;
		else
			markdead(rc, status);
	}
}

void initpass(void)
{
	struct svcrec* rc;
	int running = 0;

	setpasstime();

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
		gg.state |= S_REBOOT;
}

void stopall(void)
{
	struct svcrec* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		rc->flags |= P_DISABLED;

	gg.state |= S_PASSREQ;
}
