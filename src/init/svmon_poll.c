#include <bits/time.h>
#include <sys/signal.h>
#include <sys/sleep.h>
#include <sys/poll.h>
#include <sys/file.h>
#include <sys/mmap.h>

#include <sigset.h>
#include <format.h>
#include <null.h>
#include <exit.h>

#include "svmon.h"

static sigset_t defsigset;
static struct timespec timetowait;

/* There's always ctlfd in pfds[0], so the indexes for pfds[]
   are shifted by one compared to rings[] and recs[]. */

static struct pollfd pfds[MAXRECS+1];
static struct ringbuf rings[MAXRECS];

/* A single handler for all signals we care about. */

static void sighandler(int sig)
{
	switch(sig)
	{
		case SIGINT:
		case SIGTERM:
			gg.rbcode = 'r';
			stopall();
			break;
		case SIGCHLD:
			gg.state |= S_SIGCHLD;
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

void wakeupin(int seconds)
{
	time_t ttw = timetowait.sec;

	if(ttw >= 0 && ttw < seconds)
		return;

	timetowait.sec = seconds;
	timetowait.nsec = 0;
}

static void setfd(int fi, int fd)
{
	if(pfds[fi].fd > 0)
		sys_close(pfds[fi].fd);

	pfds[fi].fd = fd;
	pfds[fi].events = POLLIN;
}

void setctrlfd(int fd)
{
	setfd(0, fd);
}

void setpollfd(struct svcrec* rc, int fd)
{
	setfd(recindex(rc) + 1, fd); 
}

struct ringbuf* ringfor(struct svcrec* rc)
{
	int ri = recindex(rc);
	struct ringbuf* rg = &rings[ri];

	return rg->buf ? rg : NULL;
}

void flushring(struct svcrec* rc)
{
	struct ringbuf* rg = ringfor(rc);

	if(!rg) return;

	sys_munmap(rg->buf, RINGSIZE);

	rg->buf = NULL;
	rg->ptr = 0;
}

static int mmapring(struct ringbuf* rg)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	long ret = sys_mmap(NULL, RINGSIZE, prot, flags, -1, 0);

	if(mmap_error(ret)) {
		return 0;
	} else {
		rg->buf = (char*)ret;
		rg->ptr = 0;
		return 1;
	}
}

static void readring(struct ringbuf* rg, int fd)
{
	int off = rg->ptr % RINGSIZE;

	char* start = rg->buf + off;
	int avail = RINGSIZE - off;

	int rd = sys_read(fd, start, avail);

	if(rd <= 0) return;

	int ptr = rg->ptr + rd;

	if(ptr >= RINGSIZE)
		ptr = RINGSIZE + ptr % RINGSIZE;

	rg->ptr = ptr;
}

static void bufoutput(int fd, int fdi, int rbi)
{
	struct ringbuf* rg = &rings[rbi];

	if(!rg->buf && !mmapring(rg))
		pfds[fdi].fd = -1;
	else
		readring(rg, fd);
}

static void checkfds(int nr)
{
	int i;

	for(i = 0; i < nr; i++) {
		int fd = pfds[i].fd;
		int re = pfds[i].revents;

		if(re & POLLIN) {
			if(i == 0)
				acceptctl(fd);
			else
				bufoutput(fd, i, i-1);
		} if(re & POLLHUP) {
			pfds[i].fd = -1;
		}
	}
}

static void msleep(int ms)
{
	struct timespec sp = { ms/1000, (ms%1000) * 1000000 };
	sys_nanosleep(&sp, NULL);
}

void waitpoll(void)
{
	int nfds = gg.nr + 1;
	struct timespec* ts;

	if(timetowait.sec > 0 || timetowait.nsec > 0)
		ts = &timetowait;
	else
		ts = NULL;

	int r = sys_ppoll(pfds, nfds, ts, &defsigset);

	if(r == -EINTR) {
		/* we're ok here, sighandlers did their job */
	} else if(r < 0) {
		report("ppoll", NULL, r);
		msleep(1000);
	} else if(r > 0) {
		checkfds(nfds);
	} else { /* timeout has been reached */
		timetowait.sec = 0;
		timetowait.nsec = 0;
		gg.state |= S_PASSREQ;
	}
}
