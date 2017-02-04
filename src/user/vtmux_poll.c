#include <sys/ppoll.h>
#include <sys/sigprocmask.h>
#include <sys/sigaction.h>

#include <sigset.h>
#include <fail.h>

#include "vtmux.h"

#define PFDS (CONSOLES + KEYBOARDS + 1)

int nfds;
struct pollfd pfds[PFDS];
static sigset_t defsigset;
int sigterm;
int sigchld;
int fdsready;

void request_fds_update(void)
{
	fdsready = 0;
}

static void sighandler(int sig)
{
	switch(sig)
	{
		case SIGINT:
		case SIGTERM:
			sigterm = 1;
			break;
		case SIGCHLD:
			sigchld = 1;
			break;
	}
}

void setup_signals(void)
{
	struct sigaction sa = {
		.sa_handler = sighandler,
		.sa_flags = SA_RESTART | SA_RESTORER,
		.sa_restorer = sigreturn
	};

	int ret = 0;

	sigemptyset(&sa.sa_mask);
	sigaddset(&sa.sa_mask, SIGCHLD);
	ret |= syssigprocmask(SIG_BLOCK, &sa.sa_mask, &defsigset);

	sigaddset(&sa.sa_mask, SIGINT);
	sigaddset(&sa.sa_mask, SIGTERM);
	sigaddset(&sa.sa_mask, SIGHUP);

	ret |= syssigaction(SIGINT,  &sa, NULL);
	ret |= syssigaction(SIGTERM, &sa, NULL);
	ret |= syssigaction(SIGHUP,  &sa, NULL);

	/* SIGCHLD is only allowed to arrive in ppoll,
	   so SA_RESTART just does not make sense. */
	sa.sa_flags &= ~SA_RESTART;
	ret |= syssigaction(SIGCHLD, &sa, NULL);

	if(ret) fail("signal init failed", NULL, 0);
}

void update_poll_fds(void)
{
	int i;
	int j = 0;

	for(i = 0; i < nconsoles && j < PFDS; i++, j++)
		pfds[j].fd = consoles[i].ctlfd;

	for(i = 0; i < nkeyboards && j < PFDS; i++, j++)
		pfds[j].fd = keyboards[i].fd;

	for(i = 0; i < j; i++)
		pfds[i].events = POLLIN;

	nfds = j;
}

void check_polled_fds(void)
{
	int j;

	for(j = 0; j < nfds; j++)
		if(!pfds[j].revents)
			continue;
		else if(!(pfds[j].revents & POLLIN))
			pfds[j].fd = -1;
		else if(j < nconsoles)
			handlectl(j, pfds[j].fd);
		else if(j < nconsoles + nkeyboards)
			handlekbd(j - nconsoles, pfds[j].fd);
}

void mainloop(void)
{
	sigterm = 0;

	while(!sigterm)
	{
		sigchld = 0;

		if(!fdsready)
			update_poll_fds();

		fdsready = 1;

		int r = sysppoll(pfds, nfds, NULL, &defsigset);

		if(sigchld)
			waitpids();
		if(r == -EINTR)
			; /* signal has been caught and handled */
		else if(r < 0)
			fail("ppoll", NULL, r);
		else if(r > 0)
			check_polled_fds();
	}

	shutdown();
}
