#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/time.h>

#include <printf.h>
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
struct pollfd pfds[2+NCONNS+NDHCPS];
static int pollkey[2+NCONNS+NDHCPS];
int pollset;
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
	for(i = 0; i < ndhcps; i++)
		set_pollfd(dhcps[i].fd, -1 - i);

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

static void check_dhcp(struct pollfd* pf, struct dhcp* dh)
{
	if(pf->revents & POLLIN)
		handle_dhcp(dh);
	if(pf->revents & ~POLLIN)
		dhcp_error(dh);
}

static void check_polled_fds(void)
{
	int i, k;

	check_netlink(pfds[0].revents);
	check_control(pfds[1].revents);

	for(i = 2; i < npfds; i++)
		if((k = pollkey[i]) > 0)
			check_conn(&pfds[i], &conns[k - 1]);
		else if(k < 0)
			check_dhcp(&pfds[i], &dhcps[-k - 1]);

	if(pollset) return;

	update_pollfds();
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

static void stop_all_links(void)
{
	struct link* ls;

	for(ls = links; ls < links + nlinks; ls++)
		if(ls->ifi && ls->mode != LM_SKIP)
			disable_iface(ls);
}

static struct timespec* prep_poll_timer(struct timespec* t0, struct timespec* t1)
{
	struct timespec ts = { 0, 0 };

	prep_dhcp_timeout(&ts);

	if(!ts.sec && !ts.nsec)
		return NULL;

	*t0 = ts;
	*t1 = ts;

	return t1;
}

static void update_timers(struct timespec* t0, struct timespec* t1)
{
	struct timespec dt;

	dt.sec = t0->sec - t1->sec;
	dt.nsec = t0->nsec - t1->nsec;

	if(t0->nsec < t1->nsec) {
		dt.nsec += 1000*1000*1000;
		dt.sec--;
	}

	update_dhcp_timers(&dt);
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
			update_timers(&t0, &t1);
		if(r > 0)
			check_polled_fds();
	}

	stop_wait_procs();
	stop_all_links();
	save_flagged_links();
	unlink_ctrl();

	return 0;
}
