#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/time.h>

#include <netlink.h>
#include <sigset.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "ifmon.h"

ERRTAG("ifmon");

char** environ;

static sigset_t defsigset;
struct pollfd pfds[2+NCONNS];
static int pollkey[2+NCONNS];
int pollset;
int npfds;

int sigterm;
int sigchld;

void quit(const char* msg, char* arg, int err)
{
	unlink_ctrl();
	fail(msg, arg, err);
}

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM: sigterm = 1; break;
		case SIGCHLD: sigchld = 1; break;
	}
}

static void sigaction(int sig, struct sigaction* sa)
{
	int ret;

	if((ret = sys_sigaction(sig, sa, NULL)) < 0)
		quit("sigaction", NULL, ret);
}

static void sigprocmask(int sig, sigset_t* mask, sigset_t* mold)
{
	int ret;

	if((ret = sys_sigprocmask(sig, mask, mold)) < 0)
		quit("sigprocmask", NULL, ret);
}

void setup_signals(void)
{
	SIGHANDLER(sa, sighandler, SA_RESTART);

	sigaddset(&sa.mask, SIGCHLD);
	sigprocmask(SIG_BLOCK, &sa.mask, &defsigset);

	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);
	sigaddset(&sa.mask, SIGALRM);

	sigaction(SIGINT,  &sa);
	sigaction(SIGTERM, &sa);
	sigaction(SIGHUP,  &sa);
	sigaction(SIGALRM, &sa);

	sa.flags &= ~SA_RESTART;
	sigaction(SIGCHLD, &sa);

	sa.handler = SIG_IGN;
	sigaction(SIGPIPE, &sa);
}

static void set_pollfd(int fd, int tag)
{
	int i = npfds;

	if(fd <= 0)
		return;

	pfds[i].fd = fd;
	pfds[i].events = POLLIN;
	pollkey[i] = tag;

	npfds++;
}

void update_pollfds(void)
{
	int i;

	npfds = 2;

	for(i = 0; i < nconns; i++)
		set_pollfd(conns[i].fd,  1 + i);

	pollset = 1;
}

void setup_pollfds(void)
{
	npfds = 0;

	set_pollfd(netlink, 0);
	set_pollfd(ctrlfd, 0);

	update_pollfds();
}

static void check_netlink(int revents)
{
	if(revents & POLLIN)
		handle_rtnl();
	if(revents & ~POLLIN)
		quit("lost netlink connection", NULL, 0);
}

static void check_control(int revents)
{
	if(revents & POLLIN)
		accept_ctrl(ctrlfd);
	if(revents & ~POLLIN)
		quit("poll", "ctrl", 0);
}

static void close_conn(struct conn* cn)
{
	if(cn->fd <= 0)
		return;

	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));

	pollset = 0;
}

static void check_conn(struct pollfd* pf, struct conn* cn)
{
	if(!(cn->fd)) /* should not happen */
		return;
	if(pf->revents & (POLLIN | POLLHUP))
		handle_conn(cn);
	if(pf->revents & POLLHUP)
		close_conn(cn);
}

static void check_polled_fds(void)
{
	int i, k;

	check_netlink(pfds[0].revents);
	check_control(pfds[1].revents);

	for(i = 2; i < npfds; i++)
		if((k = pollkey[i]) > 0)
			check_conn(&pfds[i], &conns[k - 1]);

	if(pollset) return;

	update_pollfds();
}

static void stop_wait_procs(void)
{
	sigterm = 0;
	struct timespec ts = { 1, 0 };

	kill_all_procs(NULL);

	while(1) {
		if(!any_procs_left())
			break;
		if(sys_ppoll(NULL, 0, &ts, &defsigset) < 0)
			break;
		if(sigchld)
			got_sigchld();
		if(sigterm)
			break;
	}
}

int main(int argc, char** argv)
{
	if(argc > 1)
		fail("too many arguments", NULL, 0);

	environ = argv + argc + 1;

	setup_ctrl();
	setup_rtnl();

	setup_signals();
	setup_pollfds();

	while(!sigterm) {
		sigchld = 0;

		int r = sys_ppoll(pfds, npfds, NULL, &defsigset);

		if(sigchld)
			got_sigchld();
		if(r == -EINTR)
			; /* signal has been caught and handled */
		else if(r < 0)
			quit("ppoll", NULL, r);
		if(r > 0)
			check_polled_fds();
	}

	stop_wait_procs();
	unlink_ctrl();

	return 0;
}
