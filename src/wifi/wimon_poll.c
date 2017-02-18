#include <sys/ppoll.h>
#include <sys/sigprocmask.h>
#include <sys/sigaction.h>

#include <netlink.h>
#include <sigset.h>
#include <fail.h>

#include "wimon.h"

#define NFDS 3

int rtnlfd;
int genlfd;
int ctrlfd;

struct pollfd pfds[NFDS];
static sigset_t defsigset;

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

void setup_pollfds(void)
{
	pfds[0].fd = rtnl.fd > 0 ? rtnl.fd : -1;
	pfds[0].events = POLLIN;

	pfds[1].fd = genl.fd > 0 ? genl.fd : -1;
	pfds[1].events = POLLIN;

	pfds[2].fd = ctrlfd > 0 ? ctrlfd : -1;
	pfds[2].events = POLLIN;
}

static void recv_netlink(int revents, char* tag, struct netlink* nl,
                         void (*handle)(struct nlmsg*))
{
	if(!revents)
		return;
	if(revents & ~POLLIN)
		fail("poll", tag, 0);

	int ret;
	struct nlmsg* msg;

	if((ret = nl_recv_nowait(nl)) <= 0)
		fail("recv", tag, ret);
	while((msg = nl_get_nowait(nl))) {
		handle(msg);
	}; nl_shift_rxbuf(nl);
}

static void check_polled_fds(void)
{
	recv_netlink(pfds[0].revents, "rtnl", &rtnl, handle_rtnl);
	recv_netlink(pfds[1].revents, "genl", &genl, handle_genl);

	if(pfds[2].revents & POLLIN)
		accept_ctrl(ctrlfd);	
}

void mainloop(void)
{
	while(!sigterm)
	{
		sigchld = 0;

		int r = sysppoll(pfds, NFDS, NULL, &defsigset);

		if(sigchld)
			waitpids();
		if(r == -EINTR)
			; /* signal has been caught and handled */
		else if(r < 0)
			fail("ppoll", NULL, r);
		else if(r > 0)
			check_polled_fds();
	}
}
