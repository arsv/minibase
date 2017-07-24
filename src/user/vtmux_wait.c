#include <bits/ioctl/tty.h>

#include <sys/file.h>
#include <sys/kill.h>
#include <sys/wait.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "vtmux.h"

/* Non-terminal SIGCHLD handler. Close fds, deallocate VT,
   and do whatever else cleanup necessary.

   Most clients should die while active, but inactive ones may die
   as well. Background deaths should not cause VT switching.

   In case of abnormal exit, let the user read whatever the failed
   process might have printed to its stderr.

   Successful exit means logout and return to greeter. Except on
   a fixed VT, then it is probably better to restart the client.
   There's no such thing as "logout" on fixed VTs, and no login
   either, so no point in activating greeter VT.

   Restarts are not timed. Abnormal exits require user intervention,
   and normal exits are presumed to not happen too fast.

   Greeter may, and probably should, exit with 0 status if it is not
   being used for some time. There's no point in keeping it running
   in background, it will be re-started on request anyway. */

static void report_cause(int fd, int status)
{
	char msg[32];
	char* p = msg;
	char* e = msg + sizeof(msg) - 1;

	if(WIFEXITED(status)) {
		p = fmtstr(p, e, "Exit ");
		p = fmtint(p, e, WEXITSTATUS(status));
	} else {
		p = fmtstr(p, e, "Signal ");
		p = fmtint(p, e, WTERMSIG(status));
	}

	*p++ = '\n';
	int ret = sys_write(fd, msg, p - msg);

	if(ret < 0)
		warn("write", NULL, ret);
}

/* In case of abnormal exits, do not switch VT to let the user
   read whatever error messages may be there. */

static void wipe_dead(struct term* vt, int status)
{
	int tty = vt->tty;
	int ttyfd;

	sys_close(vt->ctlfd);
	disable_all_devs_for(tty);

	vt->ctlfd = 0;
	vt->pid = 0;

	if(status && (ttyfd = open_tty_device(tty)) >= 0) {
		ioctl(ttyfd, KDSETMODE, 0, "KDSETMODE");
		report_cause(ttyfd, status);
		sys_close(ttyfd);
	}

	if(tty == greetertty)
		greetertty = 0;

	free_term_slot(vt);

	pollset = 0;
}

void wait_pids(int shutdown)
{
	int pid, status;
	struct term *vt;
	int active = 0;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		if(!(vt = find_term_by_pid(pid)))
			continue;
		if(vt->tty == activetty && !status)
			active = 1;

		wipe_dead(vt, status);
	}

	if(!active || shutdown)
		return;

	switchto(primarytty);
}

/* Shutdown routines: wait for VT clients to die before exiting. */

int count_running(void)
{
	int count = 0;
	struct term* cvt;

	for(cvt = terms; cvt < terms + nterms; cvt++)
		if(cvt->pid > 0)
			count++;

	return count;
}

static void kill_all_terms(int sig)
{
	struct term* cvt;

	for(cvt = terms; cvt < terms + nterms; cvt++)
		if(cvt->pid > 0)
			sys_kill(cvt->pid, sig);
}

void terminate_children(void)
{
	warn("shutdown", NULL, 0);

	kill_all_terms(SIGTERM);

	if(poll_final(1) >= 0)
		return;

	warn("waiting for children to terminate", NULL, 0);

	if(poll_final(4) >= 0)
		return;

	warn("sending SIGKILL to all children", NULL, 0);
	kill_all_terms(SIGKILL);

	if(poll_final(1) >= 0)
		return;

	warn("timeout waiting for children", NULL, 0);
}
