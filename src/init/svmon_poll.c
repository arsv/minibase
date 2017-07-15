#include <bits/time.h>
#include <sys/signal.h>
#include <sys/clock.h>
#include <sys/sleep.h>
#include <sys/poll.h>
#include <sys/file.h>
#include <sys/mmap.h>

#include <sigset.h>
#include <format.h>
#include <string.h>
#include <null.h>
#include <exit.h>

#include "svmon.h"

static sigset_t defsigset;
static struct timespec timetowait;
time_t passtime;

#define NPFDS (1+NCONNS+NPROCS)

static struct pollfd pfds[NPFDS];
static int npfds;
static short pfdkeys[NPFDS];

/* A single handler for all signals we care about. */

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM:
			stop_all_procs();
			break;
		case SIGCHLD:
			gg.sigchld = 1;
			break;
	}
}

int setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};
	/* The stuff below *can* fail due to broken libc, but that is so bad
	   by itself that there is no point in reporting it properly. */
	int ret = 0;

	sigemptyset(&sa.mask);
	sigaddset(&sa.mask, SIGCHLD);
	ret |= sys_sigprocmask(SIG_BLOCK, &sa.mask, &defsigset);

	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGPWR);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);

	ret |= sys_sigaction(SIGINT,  &sa, NULL);
	ret |= sys_sigaction(SIGPWR,  &sa, NULL);
	ret |= sys_sigaction(SIGTERM, &sa, NULL);
	ret |= sys_sigaction(SIGHUP,  &sa, NULL);

	/* SIGCHLD is only allowed to arrive in ppoll,
	   so SA_RESTART just does not make sense. */
	sa.flags &= ~SA_RESTART;
	ret |= sys_sigaction(SIGCHLD, &sa, NULL);

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

static void unmap_ring_buf(struct proc* rc)
{
	sys_munmap(rc->buf, RINGSIZE);

	rc->buf = NULL;
	rc->ptr = 0;
}

void flush_ring_buf(struct proc* rc)
{
	unmap_ring_buf(rc);
}

static int mmap_ring_buf(struct proc* rc)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	long ret = sys_mmap(NULL, RINGSIZE, prot, flags, -1, 0);

	if(mmap_error(ret))
		return ret;

	rc->buf = (char*)ret;
	rc->ptr = 0;

	return 0;
}

static int read_into_ring_buf(struct proc* rc, int fd)
{
	int ret;

	if(rc->buf)
		;
	else if((ret = mmap_ring_buf(rc)) < 0)
		return ret;

	int off = rc->ptr % RINGSIZE;

	char* start = rc->buf + off;
	int avail = RINGSIZE - off;

	if((ret = sys_read(fd, start, avail)) <= 0)
		return ret;

	int ptr = rc->ptr + ret;

	if(ptr >= RINGSIZE)
		ptr = RINGSIZE + ptr % RINGSIZE;

	rc->ptr = ptr;

	return ret;
}

static void close_proc_pipe(struct proc* rc)
{
	sys_close(rc->pipefd);
	rc->pipefd = -1;
	gg.pollset = 0;
}

static void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
	gg.pollset = 0;
}

static void close_ctrl(int fd)
{
	sys_close(fd);
	gg.ctrlfd = -1;
	gg.pollset = 0;
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

static void update_poll_fds(void)
{
	int i, fd;

	npfds = 0;

	if(gg.ctrlfd > 0)
		add_polled_fd(gg.ctrlfd, 0);

	for(i = 0; i < nconns; i++)
		if((fd = conns[i].fd) > 0)
			add_polled_fd(fd, -1 - i);

	for(i = 0; i < nprocs; i++)
		if((fd = procs[i].pipefd) > 0)
			add_polled_fd(fd, 1 + i);

	gg.pollset = 1;
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

	if(!gg.pollset)
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
		gg.passreq = 1;
	}
}
