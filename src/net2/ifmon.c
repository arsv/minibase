#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>

#include <errtag.h>
#include <netlink.h>
#include <sigset.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "ifmon.h"

ERRTAG("ifmon");

char** environ;

static sigset_t defsigset;
struct pollfd pfds[2+NCONNS];
static int pollset;
int npfds;
int nconns;
int ctrlfd;

struct conn conns[NCONNS];
struct link links[NLINKS];

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

static void sigaction(int sig, struct sigaction* sa)
{
	int ret;

	if((ret = sys_sigaction(sig, sa, NULL)) < 0)
		fail("sigaction", NULL, ret);
}

static void sigprocmask(int sig, sigset_t* mask, sigset_t* mold)
{
	int ret;

	if((ret = sys_sigprocmask(sig, mask, mold)) < 0)
		fail("sigprocmask", NULL, ret);
}

void setup_signals(void)
{
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};

	sigemptyset(&sa.mask);
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

static void set_pollfd(struct pollfd* pfd, int fd)
{
	if(fd > 0) {
		pfd->fd = fd;
		pfd->events = POLLIN;
	} else {
		pfd->fd = -1;
		pfd->events = 0;
	}
}

void update_connfds(void)
{
	int i;

	for(i = 0; i < nconns; i++)
		set_pollfd(&pfds[2+i], conns[i].fd);

	npfds = 2 + nconns;

	pollset = 1;
}

void setup_pollfds(void)
{
	set_pollfd(&pfds[0], netlink);
	set_pollfd(&pfds[1], ctrlfd);

	update_connfds();
}

static void recv_netlink(int revents)
{
	if(revents & POLLIN)
		handle_rtnl();
	if(revents & ~POLLIN)
		fail("lost netlink connection", NULL, 0);
}

static void recv_control(int revents)
{
	if(revents & POLLIN) {
		accept_ctrl(ctrlfd);
		pollset = 0;
	} if(revents & ~POLLIN) {
		fail("poll", "ctrl", 0);
	}
}

static void close_conn(struct conn* cn)
{
	if(cn->fd <= 0)
		return;

	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));

	pollset = 0;
}

static void recv_client(struct pollfd* pf, struct conn* cn)
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
	int i;

	recv_netlink(pfds[0].revents);
	recv_control(pfds[1].revents);

	for(i = 0; i < nconns; i++)
		recv_client(&pfds[2+i], &conns[i]);

	if(pollset)
		return;

	update_connfds();
}

static void stop_wait_procs(void)
{
	sigterm = 0;
	struct timespec ts = { 1, 0 };

	kill_all_procs(NULL);

	while(1) {
		if(!any_procs_left(NULL))
			break;
		if(sys_ppoll(NULL, 0, &ts, &defsigset) < 0)
			break;
		if(sigchld)
			waitpids();
		if(sigterm)
			break;
	}
}

int main(int argc, char** argv, char** envp)
{
	(void)argv;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	environ = envp;

	setup_ctrl();
	setup_rtnl();

	setup_signals();
	setup_pollfds();

	while(!sigterm) {
		sigchld = 0;

		int r = sys_ppoll(pfds, npfds, NULL, &defsigset);

		if(sigchld)
			waitpids();
		if(r == -EINTR)
			; /* signal has been caught and handled */
		else if(r < 0)
			fail("ppoll", NULL, r);
		else if(r > 0)
			check_polled_fds();
	}

	stop_wait_procs();
	save_flagged_links();
	unlink_ctrl();

	return 0;
}
