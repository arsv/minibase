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
uint pfdkeys[PFDS];

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
		.flags = SA_RESTORER,
		.restorer = sigreturn
	};
	sigset_t* mask = &sa.mask;

	sigemptyset(mask);
	sigemptyset(&defsigset);
	sigaddset(mask, SIGCHLD);

	sigprocmask(SIG_BLOCK, mask, NULL);

	/* avoid cross-invoking these */
	sigaddset(mask, SIGUSR1);
	sigaddset(mask, SIGUSR2);
	sigaddset(mask, SIGALRM);

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGHUP,  &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);
	sigaction(SIGUSR1, &sa, NULL);
	sigaction(SIGUSR2, &sa, NULL);
	sigaction(SIGCHLD, &sa, NULL);
}


/* Failing fds are dealt with immediately, to avoid re-queueing
   them from ppoll. For keyboards, this is also the only place
   where the fds get closed. Control fds are closed either here
   or in closevt(). */

static void recv_ctrl(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		accept_ctrl();
	if(!(pf->revents & ~POLLIN))
		return;

	sys_close(ctrlfd);
	ctrlfd = -1;

	pf->fd = -1;
	pollset = 0;
}

static void check_pipe(struct pollfd* pf, struct term* vt)
{
	if(pf->revents & POLLIN)
		recv_pipe(vt);
	if(!(pf->revents & ~POLLIN))
		return;

	sys_close(vt->ctlfd);
	vt->ctlfd = -1;

	pf->fd = -1;
	pollset = 0;
}

static void check_term(struct pollfd* pf, struct term* vt)
{
	if(!pf->revents)
		return;

	final_enter(vt);

	if(!(pf->revents & ~POLLIN))
		return;

	pf->fd = -1;
	pollset = 0;
}

static void check_conn(struct pollfd* pf, struct conn* cn)
{
	if(pf->revents & POLLIN)
		recv_conn(cn);
	if(!(pf->revents & ~POLLIN))
		return;

	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));

	pf->fd = -1;
	pollset = 0;
}

#define CTLFD  1
#define TTYFD  2
#define CONNFD 3

#define CTL(i) ((CTLFD << 16) | (i))
#define TTY(i) ((TTYFD << 16) | (i))
#define CFD(i) ((CONNFD << 16) | (i))

#define TAG(k) ((k) >> 16)
#define IDX(k) ((k) & 0xFFFF)

static int add_poll_fd(int n, int fd, uint tag)
{
	if(fd <= 0)
		return n;

	struct pollfd* pf = &pfds[n];

	pf->fd = fd;
	pf->events = POLLIN;
	pfdkeys[n] = tag;

	return n + 1;
}

static int update_poll_fds(void)
{
	int i, n = 0;

	n = add_poll_fd(n, ctrlfd, 0);

	for(i = 0; i < nterms; i++)
		if(terms[i].pid > 0)
			n = add_poll_fd(n, terms[i].ctlfd, CTL(i));
		else
			n = add_poll_fd(n, terms[i].ttyfd, TTY(i));

	for(i = 0; i < nconns; i++)
		n = add_poll_fd(n, conns[i].fd, CFD(i));

	pollset = 1;

	return n;
}

void check_polled_fds(int n)
{
	uint i;

	recv_ctrl(&pfds[0]);

	for(i = 1; i < n; i++) {
		uint key = pfdkeys[i];
		int tag = key >> 16;
		int idx = key & 0xFFFF;

		if(tag == CONNFD)
			check_conn(&pfds[i], &conns[idx]);
		else if(tag == CTLFD)
			check_pipe(&pfds[i], &terms[idx]);
		else if(tag == TTYFD)
			check_term(&pfds[i], &terms[idx]);
		else
			warn("bad pollfd tag", NULL, tag);
	}
}

void poll_inputs(void)
{
	int ret, n;

	pollset = 0;

	while(!sigterm) {
		if(!pollset)
			n = update_poll_fds();

		ret = sys_ppoll(pfds, n, NULL, &defsigset);

		if(sigchld)
			wait_pids(0);
		if(ret == -EINTR)
			; /* signal has been caught and handled */
		else if(ret < 0)
			quit("ppoll", NULL, ret);
		else if(ret > 0)
			check_polled_fds(n);
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
