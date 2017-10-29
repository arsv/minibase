#include <sys/signal.h>
#include <sys/file.h>
#include <sys/time.h>
#include <sys/sched.h>
#include <sys/ppoll.h>

#include <sigset.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "super.h"

#define BOOTCLOCKOFFSET 1000
#define NPFDS (1+NCONNS+NPROCS)

static sigset_t defsigset;
static struct timespec timetowait;
time_t passtime;

static struct pollfd pfds[NPFDS];
static int npfds;
static short pfdkeys[NPFDS];
static short pollset;

/* A single handler for all signals we care about. */

static void sighandler(int sig)
{
	switch(sig) {
		case SIGPWR:
			rbcode = 'p';
			/* fallthrough */
		case SIGINT:
		case SIGTERM:
			stop_all_procs();
			break;
		case SIGCHLD:
			request(F_WAIT_PIDS);
			break;
	}
}

static int sigprocmask(int how, sigset_t* mask, sigset_t* mold)
{
	int ret;

	if((ret = sys_sigprocmask(how, mask, mold)) < 0)
		report("sigprocmask", NULL, ret);

	return ret;
}

static int sigaction(int sig, struct sigaction* sa, char* tag)
{
	int ret;

	if((ret = sys_sigaction(sig, sa, NULL)) < 0)
		report("sigaction", tag, ret);

	return ret;
}

int setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};

	int ret = 0;

	sigemptyset(&sa.mask);
	sigaddset(&sa.mask, SIGCHLD);
	ret |= sigprocmask(SIG_BLOCK, &sa.mask, &defsigset);

	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGPWR);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);

	ret |= sigaction(SIGINT,  &sa, NULL);
	ret |= sigaction(SIGPWR,  &sa, NULL);
	ret |= sigaction(SIGTERM, &sa, NULL);
	ret |= sigaction(SIGHUP,  &sa, NULL);

	/* SIGCHLD is only allowed to arrive in ppoll,
	   so SA_RESTART just does not make sense. */
	sa.flags &= ~SA_RESTART;

	ret |= sigaction(SIGCHLD, &sa, NULL);

	if(ret) report("signal setup failed", NULL, 0);

	return ret;
}

void set_passtime(void)
{
	struct timespec tp = { 0, 0 };
	long ret;

	if((ret = sys_clock_gettime(CLOCK_MONOTONIC, &tp)) < 0)
		report("clock_gettime", "CLOCK_MONOTONIC", ret);
	else
		passtime = BOOTCLOCKOFFSET + tp.sec;
}

void wakeupin(int seconds)
{
	time_t ttw = timetowait.sec;

	if(ttw >= 0 && ttw < seconds)
		return;

	timetowait.sec = seconds;
	timetowait.nsec = 0;
}

static void close_proc_pipe(struct proc* rc)
{
	sys_close(rc->pipefd);
	rc->pipefd = -1;
	pollset = 0;
}

static void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
	pollset = 0;
}

static void close_ctrl(int fd)
{
	sys_close(fd);
	ctrlfd = -1;
	pollset = 0;
}

static void recv_ctrl(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		accept_ctrl(pf->fd);
	if(pf->revents & ~POLLIN)
		close_ctrl(pf->fd);
}

static void recv_conn(struct pollfd* pf, struct conn* cn)
{
	if(pf->revents & POLLIN)
		handle_conn(cn);
	if(pf->revents & ~POLLIN)
		close_conn(cn);
}

static void recv_proc(struct pollfd* pf, struct proc* rc)
{
	if(pf->revents & POLLIN)
		if(read_into_ring_buf(rc, pf->fd) >= 0)
			return;
	if(pf->revents)
		close_proc_pipe(rc);
}

static void check_polled_fds(void)
{
	int i, key;

	for(i = 0; i < npfds; i++)
		if((key = pfdkeys[i]) == 0)
			recv_ctrl(&pfds[i]);
		else if(key > 0)
			recv_proc(&pfds[i], &procs[key-1]);
		else if(key < 0)
			recv_conn(&pfds[i], &conns[-key-1]);
}

static void add_polled_fd(int fd, int key)
{
	if(npfds >= NPFDS)
		return;

	int i = npfds++;
	struct pollfd* pf = &pfds[i];

	pf->fd = fd;
	pf->events = POLLIN;

	pfdkeys[i] = key;
}

void update_poll_fds(void)
{
	int i, fd;

	npfds = 0;

	if(ctrlfd > 0)
		add_polled_fd(ctrlfd, 0);

	for(i = 0; i < nconns; i++)
		if((fd = conns[i].fd) > 0)
			add_polled_fd(fd, -1 - i);

	for(i = 0; i < nprocs; i++)
		if((fd = procs[i].pipefd) > 0)
			add_polled_fd(fd, 1 + i);
}

static void msleep(int ms)
{
	struct timespec sp = { ms/1000, (ms%1000) * 1000000 };
	sys_nanosleep(&sp, NULL);
}

void wait_poll(void)
{
	struct timespec* ts;

	if(timetowait.sec > 0 || timetowait.nsec > 0)
		ts = &timetowait;
	else
		ts = NULL;

	if(!pollset)
		update_poll_fds();

	passtime = 0;

	int r = sys_ppoll(pfds, npfds, ts, &defsigset);

	if(r == -EINTR) {
		/* we're ok here, sighandlers did their job */
	} else if(r < 0) {
		report("ppoll", NULL, r);
		msleep(1000);
	} else if(r > 0) {
		check_polled_fds();
	} else { /* timeout has been reached */
		timetowait.sec = 0;
		timetowait.nsec = 0;
		request(F_CHECK_PROCS);
	}
}
