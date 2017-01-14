#include <sys/fork.h>
#include <sys/execve.h>
#include <sys/kill.h>
#include <sys/_exit.h>

#include <format.h>
#include <string.h>

#include "svcmon.h"

static int waitneeded(time_t* last, time_t wait)
{
	time_t curtime = gg.passtime;
	time_t endtime = *last + wait;

	if(endtime <= curtime) {
		*last = gg.passtime;
		return 0;
	} else {
		int ttw = endtime - curtime;
		if(gg.timetowait < 0 || gg.timetowait > ttw)
			gg.timetowait = ttw;
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

	int pid = sysfork();

	if(pid < 0)
		return;

	if(pid == 0) {
		_exit(child(rc));
	} else {
		rc->pid = pid;
		rc->lastrun = gg.passtime;
		rc->lastsig = 0;
	}
}

static void stop(struct svcrec* rc)
{
	if(rc->pid <= 0)
		/* This can only happen on telinit stop, so let the user know */
		return reprec(rc, "not running");

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

		rc->lastsig = gg.passtime;
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

void initpass(void)
{
	struct svcrec* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc)) {
		int disabled = (rc->flags & P_DISABLED);

		if(rc->pid <= 0 && !disabled)
			spawn(rc);
		else if(rc->pid > 0 && disabled)
			stop(rc);
	}
}

void killpass(void)
{
	struct svcrec* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		if(rc->pid > 0)
			stop(rc);
}

int anyrunning(void)
{
	struct svcrec* rc;

	for(rc = firstrec(); rc; rc = nextrec(rc))
		if(rc->pid > 0)
			return 1;

	return 0;
}
