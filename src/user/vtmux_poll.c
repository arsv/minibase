#include <sys/file.h>
#include <sys/poll.h>
#include <sys/kill.h>
#include <sys/wait.h>
#include <sys/signal.h>
#include <sys/fsnod.h>

#include <format.h>
#include <string.h>
#include <sigset.h>
#include <fail.h>

#include "vtmux.h"

#define PFDS (1 + NTERMS + NCONNS)

static sigset_t defsigset;
struct pollfd pfds[PFDS];
int npfds, pfdkeys[PFDS];

int pollset;
int sigterm;
int sigchld;

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM: sigterm = 1; break;
		case SIGCHLD: sigchld = 1; break;
	}
}

void setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};

	int ret = 0;

	sigemptyset(&sa.mask);
	sigaddset(&sa.mask, SIGCHLD);
	ret |= sys_sigprocmask(SIG_BLOCK, &sa.mask, &defsigset);

	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);

	ret |= sys_sigaction(SIGINT,  &sa, NULL);
	ret |= sys_sigaction(SIGTERM, &sa, NULL);
	ret |= sys_sigaction(SIGHUP,  &sa, NULL);

	/* SIGCHLD is only allowed to arrive in ppoll,
	   so SA_RESTART just does not make sense. */
	sa.flags &= ~SA_RESTART;
	ret |= sys_sigaction(SIGCHLD, &sa, NULL);

	if(ret) fail("signal init failed", NULL, 0);
}

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

/* No idea why this is necessary, but the open tty fd somehow gets
   invalidated when the child exits. May be related to setsid.
   Anyway, to do anything with the tty we have to re-open it.

   There are few cases when the newly-opened fd will be closed
   immediately in closevt(), however skipping reopen just isn't
   worth the trouble. */

static void reopen_tty_device(struct term* vt)
{
	int fd;

	sys_close(vt->ttyfd);
	vt->ttyfd = 0;

	if((fd = open_tty_device(vt->tty)) < 0)
		return;

	vt->ttyfd = fd;
}

static void wait_pids(int shutdown)
{
	int status;
	int pid, ret;
	struct term *cvt, *active = NULL;

	while((pid = sys_waitpid(-1, &status, WNOHANG)) > 0) {
		if(!(cvt = find_term_by_pid(pid)))
			continue;

		reopen_tty_device(cvt);

		if(status && cvt->ttyfd > 0)
			report_cause(cvt->ttyfd, status);
		if(cvt->tty == activetty && !status)
			active = cvt;

		closevt(cvt, !!status);
	}

	if(!active || shutdown)
		return;
	else if(active->pin)
		ret = switchto(active->tty); /* try to restart it */
	else
		ret = switchto(terms[0].tty); /* greeter */
	if(ret < 0)
		warn("switchto", NULL, ret);
}

/* Shutdown routines: wait for VT clients to die before exiting. */

static int count_running(void)
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

static int add_poll_fd(int n, int fd, int key)
{
	if(fd <= 0)
		return n;

	struct pollfd* pf = &pfds[n];

	pf->fd = fd;
	pf->events = POLLIN;
	pfdkeys[n] = key;

	return n + 1;
}

void update_poll_fds(void)
{
	int i, n = 0;

	n = add_poll_fd(n, ctrlfd, 0);

	for(i = 0; i < nterms; i++)
		n = add_poll_fd(n, terms[i].ctlfd, 1 + i);
	for(i = 0; i < nconns; i++)
		n = add_poll_fd(n, conns[i].fd, -1 - i);

	npfds = n;
	pollset = 1;
}

/* Failing fds are dealt with immediately, to avoid re-queueing
   them from ppoll. For keyboards, this is also the only place
   where the fds get closed. Control fds are closed either here
   or in closevt(). */

static void close_pipe(struct term* vt)
{
	sys_close(vt->ctlfd);
	vt->ctlfd = -1;
	pollset = 0;
}

static void close_ctrl()
{
	sys_close(ctrlfd);
	ctrlfd = -1;
	pollset = 0;
}

static void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
	pollset = 0;
}

static void recv_ctrl(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		accept_ctrl();
	if(pf->revents & ~POLLIN)
		close_ctrl();
}

static void recv_term(struct pollfd* pf, struct term* vt)
{
	if(pf->revents & POLLIN)
		handle_pipe(vt);
	if(pf->revents & ~POLLIN)
		close_pipe(vt);
}

static void recv_conn(struct pollfd* pf, struct conn* cn)
{
	if(pf->revents & POLLIN)
		handle_conn(cn);
	if(pf->revents & ~POLLIN)
		close_conn(cn);
}

void check_polled_fds(void)
{
	int i, key;

	for(i = 0; i < npfds; i++)
		if((key = pfdkeys[i]) == 0)
			recv_ctrl(&pfds[i]);
		else if(key > 0)
			recv_term(&pfds[i], &terms[key-1]);
		else if(key < 0)
			recv_conn(&pfds[i], &conns[-key-1]);
}

void poll_inputs(void)
{
	int ret;

	while(!sigterm) {
		if(!pollset)
			update_poll_fds();

		ret = sys_ppoll(pfds, npfds, NULL, &defsigset);

		if(sigchld)
			wait_pids(0);
		if(ret == -EINTR)
			; /* signal has been caught and handled */
		else if(ret < 0)
			fail("ppoll", NULL, ret);
		else if(ret > 0)
			check_polled_fds();

		sigchld = 0;
	}

	sigterm = 0;
}

static int poll_final(int secs)
{
	struct timespec ts = { secs, 0 };
	int ret;

	while(!sigterm) {
		if(count_running() <= 0)
			break;

		ret = sys_ppoll(NULL, 0, &ts, &defsigset);

		if(sigchld)
			wait_pids(1);
		if(sigterm)
			warn("interrupted", NULL, 0);
		if(ret == -EINTR)
			; /* signal has been caught and handled */
		else if(ret < 0)
			fail("ppoll", NULL, ret);
		else if(ret == 0)
			return -ETIMEDOUT;

		sigchld = 0;
	}

	return sigterm ? -EINTR : 0;
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
