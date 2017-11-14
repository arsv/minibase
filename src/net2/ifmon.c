#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/time.h>

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
struct timespec timer;
int npfds;
int nconns;
int ctrlfd;

struct conn conns[NCONNS];
struct link links[NLINKS];

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
		quit("lost netlink connection", NULL, 0);
}

static void recv_control(int revents)
{
	if(revents & POLLIN) {
		accept_ctrl(ctrlfd);
		pollset = 0;
	} if(revents & ~POLLIN) {
		quit("poll", "ctrl", 0);
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

static struct timespec* prep_poll_timer(struct timespec* t0, struct timespec* t1)
{
	struct link* ls;
	struct timespec ts = { 0, 0 }, ct;

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->ifi)
			continue;
		if(!ls->timer)
			continue;

		if(ls->flags & LF_MILLIS) {
			ct.sec = ls->timer / 1000;
			ct.nsec = (ls->timer % 1000) * 1000*1000;
		} else {
			ct.sec = ls->timer;
			ct.nsec = 0;
		}

		if(!ts.sec && !ts.nsec)
			;
		else if(ts.sec > ct.sec)
			continue;
		else if(ts.nsec > ct.nsec)
			continue;

		ts = ct;
	}

	if(!ts.sec && !ts.nsec)
		return NULL;

	*t0 = ts;
	*t1 = ts;

	return t1;
}

static void update_link_timers(struct timespec* t0, struct timespec* t1)
{
	struct timespec diff;
	struct link* ls;
	ulong sub;

	diff.sec = t0->sec - t1->sec;
	diff.nsec = t0->nsec - t1->nsec;

	if(t1->nsec <= t0->nsec) {
		diff.nsec += 1000*1000*1000;
		diff.sec--;
	}

	for(ls = links; ls < links + nlinks; ls++) {
		if(!ls->timer)
			continue;
		if(ls->flags & LF_MILLIS)
			sub = 1000*diff.sec + diff.nsec / 1000*1000;
		else
			sub = diff.sec;

		if(ls->timer > sub) {
			ls->timer -= sub;
		} else {
			ls->timer = 0;
			link_timer(ls);
		}
	}
}

int main(int argc, char** argv, char** envp)
{
	(void)argv;
	struct timespec *pt, t0, t1;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	environ = envp;

	setup_ctrl();
	setup_rtnl();

	setup_signals();
	setup_pollfds();

	while(!sigterm) {
		sigchld = 0;

		pt = prep_poll_timer(&t0, &t1);

		int r = sys_ppoll(pfds, npfds, pt, &defsigset);

		if(sigchld)
			waitpids();
		if(r == -EINTR)
			; /* signal has been caught and handled */
		else if(r < 0)
			quit("ppoll", NULL, r);

		if(pt != NULL)
			update_link_timers(&t0, &t1);

		if(r > 0)
			check_polled_fds();
	}

	stop_wait_procs();
	save_flagged_links();
	unlink_ctrl();

	return 0;
}
