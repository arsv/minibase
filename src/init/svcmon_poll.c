#include <bits/time.h>
#include <sys/ppoll.h>
#include <sys/sigaction.h>
#include <sys/sigprocmask.h>
#include <sys/_exit.h>
#include <sys/close.h>
#include <sys/nanosleep.h>

#include <sigset.h>
#include <format.h>
#include <null.h>

#include "svcmon.h"

/* A single handler for all signals we care about. */

static sigset_t defsigset;
static struct timespec timetowait;

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

int setsignals(void)
{
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART | SA_RESTORER,
		.sa_restorer = sigreturn
	};
	/* The stuff below *can* fail due to broken libc, but that is so bad
	   by itself that there is no point in reporting it properly. */
	int ret = 0;

	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGCHLD);
	ret |= syssigprocmask(SIG_BLOCK, &sa.sa_mask, &defsigset);

	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGPWR);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGHUP);

	ret |= syssigaction(SIGINT,  &sa, NULL);
	ret |= syssigaction(SIGPWR,  &sa, NULL);
	ret |= syssigaction(SIGTERM, &sa, NULL);
	ret |= syssigaction(SIGHUP,  &sa, NULL);

	/* SIGCHLD is only allowed to arrive in ppoll,
	   so SA_RESTART just does not make sense. */
	sa.sa_flags &= ~SA_RESTART;
	ret |= syssigaction(SIGCHLD, &sa, NULL);

	return ret;
}

void wakeupin(int seconds)
{
	time_t ttw = timetowait.tv_sec;

	if(ttw >= 0 && ttw < seconds)
		return;

	timetowait.tv_sec = seconds;
	timetowait.tv_nsec = 0;
}

void setpollfd(int i, int fd)
{
	if(pfds[i].fd > 0)
		sysclose(pfds[i].fd);

	pfds[i].fd = fd;
	pfds[i].events = POLLIN;
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
				bufoutput(fd, i);
		} if(re & POLLHUP) {
			setpollfd(i, -1);
		}
	}
}

static void msleep(int ms)
{
	struct timespec sp = { ms/1000, (ms%1000) * 1000000 };
	sysnanosleep(&sp, NULL);
}

void waitpoll(void)
{
	int nfds = gg.nr + 1;
	struct timespec* ts;

	if(timetowait.tv_sec > 0 || timetowait.tv_nsec > 0)
		ts = &timetowait;
	else
		ts = NULL;

	int r = sysppoll(pfds, nfds, ts, &defsigset);

	if(r == -EINTR) {
		/* we're ok here, sighandlers did their job */
	} else if(r < 0) {
		report("ppoll", NULL, r);
		msleep(1000);
	} else if(r > 0) {
		checkfds(nfds);
	} else { /* timeout has been reached */
		timetowait.tv_sec = 0;
		timetowait.tv_nsec = 0;
		gg.state |= S_PASSREQ;
	}
}
