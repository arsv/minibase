#include <sys/ppoll.h>
#include <sys/sigaction.h>
#include <sys/sigprocmask.h>
#include <sys/_exit.h>
#include <sys/nanosleep.h>

#include <sigset.h>
#include <format.h>
#include <null.h>

#include "init.h"

/* A single handler for all signals we care about. */

static sigset_t defsigset;

static void sighandler(int sig)
{
	switch(sig)
	{
		case SIGINT:
		case SIGTERM:
			gg.rbcode = 'r';
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

void waitpoll(void)
{
	int r;
	struct pollfd pfd;
	struct timespec pts;
	struct timespec* ppts;

	pfd.fd = gg.ctlfd;
	pfd.events = POLLIN;
	if(gg.timetowait >= 0) {
		pts.tv_sec = gg.timetowait;
		pts.tv_nsec = 0;
		ppts = &pts;
	} else {
		ppts = NULL;
	}

	r = sysppoll(&pfd, 1, ppts, &defsigset);

	if(r < 0 && r != -EINTR) {
		_exit(0xFF);
	} else if(r > 0) {
		/* only one fd in pfd, so not that much choice here */
		gg.state |= S_CTRLREQ;
	} /* EINTR, on the other hand, is totally ok (SIGCHLD etc) */
}
