#include <sys/file.h>
#include <sys/poll.h>
#include <sys/kill.h>
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
int mdevreq;

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
		if(mdevreq)
			flush_mdevs();

		sigchld = 0;
		mdevreq = 0;
	}

	sigterm = 0;
}

int poll_final(int secs)
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
