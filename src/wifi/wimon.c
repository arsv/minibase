#include <bits/errno.h>
#include <sys/unlink.h>
#include <sys/ppoll.h>
#include <sys/pause.h>
#include <sys/close.h>
#include <sys/sigprocmask.h>
#include <sys/sigaction.h>

#include <netlink.h>
#include <sigset.h>
#include <fail.h>

#include "config.h"
#include "wimon.h"

ERRTAG = "wimon";
ERRLIST = {
	REPORT(ENOMEM), REPORT(EINVAL), REPORT(ENOBUFS), REPORT(EFAULT),
	REPORT(EINTR), REPORT(ENOENT), REPORT(EBUSY), REPORT(EADDRNOTAVAIL),
	REPORT(ENETDOWN),
	RESTASNUMBERS
};

char** environ;
int envcount;

#define NFDS (3 + NCONNS)

int rtnlfd;
int genlfd;
int ctrlfd;

static sigset_t defsigset;
struct pollfd pfds[NFDS];
int npfds;

int sigterm;
int sigchld;

/* Wimon is almost completely event-driven, with the events normally coming
   from RTNL/GENL sockets and rfkill device. There's control socket and client
   connections, SIGCHLD from dhcp and wpa processes, and some scheduled scans.
   It all gets handled in a single ppoll loop.

   Note wimon neither buffers nor polls any outputs. Writes to NL sockets are
   assumed to always go through fast, and writes to client connections are
   guarded with alarms. */

struct task {
	struct timespec tv;
	void (*call)(void);
} tasks[NTASKS];

void schedule(void (*call)(void), int secs)
{
	struct task* tk;
	int up;

	for(tk = tasks; tk < tasks + NTASKS; tk++)
		if(tk->call == call)
			goto got;
	for(tk = tasks; tk < tasks + NTASKS; tk++)
		if(!tk->call)
			goto got;

	return fail("too many scheduled tasks", NULL, 0);
got:
	up = tk->tv.tv_nsec > 0 ? 1 : 0;

	if(tk->tv.tv_sec <= 0 && tk->tv.tv_nsec <= 0)
		;
	else if(tk->tv.tv_sec + up <= secs)
		return;

	tk->tv.tv_sec = secs;
	tk->tv.tv_nsec = 0;
	tk->call = call;
}

static void timesub(struct timespec* ta, struct timespec* tb)
{
	int carry;

	if(ta->tv_nsec >= tb->tv_nsec) {
		carry = 0;
		ta->tv_nsec -= tb->tv_nsec;
	} else {
		carry = 1;
		ta->tv_nsec = 1000*1000*1000 - tb->tv_nsec;
	}

	ta->tv_sec -= tb->tv_sec + carry;

	if(ta->tv_nsec < 0)
		ta->tv_nsec = 0;
	if(ta->tv_sec < 0)
		ta->tv_sec = 0;
}

static struct timespec* poll_timeout(struct timespec* ts, struct timespec* te)
{
	struct task* tk;
	struct timespec *pt = NULL;

	for(tk = tasks; tk < tasks + NTASKS; tk++) {
		if(!tk->call)
			continue;
		if(!pt)
			*(pt = te) = tk->tv;
		else if(pt->tv_sec > tk->tv.tv_sec)
			continue;
		else if(pt->tv_nsec > tk->tv.tv_nsec)
			continue;
		else *pt = tk->tv;
	}

	if(pt) *ts = *pt;

	return pt;
}

static void update_sched(struct timespec* ts, struct timespec* te)
{
	struct task* tk;
	struct timespec td = *ts;

	if(!te) return;

	timesub(&td, te);

	for(tk = tasks; tk < tasks + NTASKS; tk++) {
		if(!tk->call)
			continue;

		timesub(&tk->tv, &td);

		if(tk->tv.tv_sec > 0)
			continue;
		if(tk->tv.tv_nsec > 0)
			continue;

		(tk->call)();
		tk->call = NULL;
	};
}

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
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};

	int ret = 0;

	sigemptyset(&sa.mask);
	sigaddset(&sa.mask, SIGCHLD);
	ret |= syssigprocmask(SIG_BLOCK, &sa.mask, &defsigset);

	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);
	sigaddset(&sa.mask, SIGALRM);

	ret |= syssigaction(SIGINT,  &sa, NULL);
	ret |= syssigaction(SIGTERM, &sa, NULL);
	ret |= syssigaction(SIGHUP,  &sa, NULL);
	ret |= syssigaction(SIGALRM, &sa, NULL);

	sa.flags &= ~SA_RESTART;
	ret |= syssigaction(SIGCHLD, &sa, NULL);

	if(ret) fail("signal init failed", NULL, 0);
}

/* GENL, RTNL sockets and the listening control socket remain open
   for as long as wimon runs, but wimon must be ready to re-open rfkill
   device fd (see wimon_kill.c) and client connections are expected to
   come and go quite often.

   To simplify handling, conns[0:nconns] are mapped to pfds[4:nconns+4]
   regardless of any gaps in conns[] which just get pfds[4+i].fd = -1. */

void update_killfd(void)
{
	pfds[3].fd = rfkillfd;
	pfds[3].events = POLLIN;
}

void update_connfds(void)
{
	int i;

	for(i = 0; i < nconns; i++) {
		struct conn* cn = &conns[i];
		struct pollfd* pf = &pfds[4+i];

		if(cn->fd <= 0)
			pf->fd = -1;
		else
			pf->fd = cn->fd;

		pf->events = POLLIN;
	}

	npfds = 4 + nconns;
}

void setup_pollfds(void)
{
	pfds[0].fd = rtnl.fd > 0 ? rtnl.fd : -1;
	pfds[0].events = POLLIN;

	pfds[1].fd = genl.fd > 0 ? genl.fd : -1;
	pfds[1].events = POLLIN;

	pfds[2].fd = ctrlfd > 0 ? ctrlfd : -1;
	pfds[2].events = POLLIN;

	update_connfds();
	update_killfd();
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

static void recv_socket(int revents)
{
	if(!revents)
		return;
	if(revents & POLLIN)
		accept_ctrl(ctrlfd);
	if(revents & ~POLLIN)
		fail("poll", "ctrl", 0);
}

static void recv_rfkill(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_rfkill();
	if(pf->revents & ~POLLIN) {
		sysclose(pf->fd);
		pf->fd = -1;
		reset_rfkill();
	}
}

static void recv_client(struct pollfd* pf, struct conn* cn)
{
	if(!(cn->fd)) /* should not happen */
		return;
	if(pf->revents & POLLHUP)
		cn->hup = 1;
	if(pf->revents & (POLLIN | POLLHUP))
		handle_conn(cn);
}

static void check_polled_fds(void)
{
	int i;

	recv_netlink(pfds[0].revents, "rtnl", &rtnl, handle_rtnl);
	recv_netlink(pfds[1].revents, "genl", &genl, handle_genl);
	recv_socket(pfds[2].revents);
	recv_rfkill(&pfds[3]);

	for(i = 0; i < nconns; i++)
		recv_client(&pfds[4+i], &conns[i]);

	update_connfds();
}

static void setup_env(char** envp)
{
	char** p;

	environ = envp;
	envcount = 0;

	for(p = envp; *p; p++)
		envcount++;
}

/* The children are assumed to die fast and reliably, so signals are only
   sent once and this process waits indefinitely until all of the exit.
   However, if something goes wrong, the user gets a chance to interrupt
   the wait with a second SIGTERM.

   At this point, NL requests are sent unchecked, and replies (if any)
   do not get read. Continuing with the main loop and waiting for the links
   to change state is quite complicated and completely pointless. */

static void stop_wait_procs(void)
{
	sigterm = 0;
	struct timespec ts = { 1, 0 };

	stop_all_procs();
	finalize_links();

	while(1) {
		if(!any_pids_left())
			break;
		if(sysppoll(NULL, 0, &ts, &defsigset) < 0)
			break;
		if(sigchld)
			waitpids();
		if(sigterm)
			break;
	}
}

int main(int argc, char** argv, char** envp)
{
	struct timespec ts, te, *tp;

	setup_env(envp);

	setup_rtnl();
	setup_genl();
	setup_ctrl();
	retry_rfkill();

	setup_signals();
	setup_pollfds();

	while(!sigterm)
	{
		sigchld = 0;

		tp = poll_timeout(&ts, &te);

		int r = sysppoll(pfds, npfds, tp, &defsigset);

		if(sigchld)
			waitpids();
		if(r == -EINTR)
			; /* signal has been caught and handled */
		else if(r < 0)
			fail("ppoll", NULL, r);
		else if(r > 0)
			check_polled_fds();

		update_sched(&ts, tp);
	}

	stop_wait_procs();
	unlink_ctrl();

	return 0;
}
