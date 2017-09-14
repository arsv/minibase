#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>

#include <format.h>
#include <string.h>
#include <sigset.h>
#include <util.h>

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
		case SIGALRM: switch_sigalrm(); break;
		case SIGUSR1: switch_sigusr1(); break;
	}
}

static void sigaction(int sig, struct sigaction* sa, char* tag)
{
	xchk(sys_sigaction(sig, sa, NULL), "sigaction", tag);
}

static void sigprocmask(int how, sigset_t* mask, sigset_t* out)
{
	xchk(sys_sigprocmask(how, mask, out), "sigiprocmask", "SIG_BLOCK");
}

void setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};
	sigset_t* mask = &sa.mask;

	sigemptyset(mask);
	sigaddset(mask, SIGCHLD);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);

	sigprocmask(SIG_BLOCK, mask, &defsigset);

	/* avoid cross-invoking these */
	sigaddset(mask, SIGUSR1);
	sigaddset(mask, SIGUSR2);
	sigaddset(mask, SIGALRM);

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);

	/* SIGCHLD is only allowed to arrive in ppoll,
	   so SA_RESTART just does not make sense. */
	sa.flags &= ~SA_RESTART;
	sigaction(SIGCHLD, &sa, NULL);
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
			quit("ppoll", NULL, ret);
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
