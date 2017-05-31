#include <bits/errno.h>
#include <sys/unlink.h>
#include <sys/ppoll.h>
#include <sys/pause.h>
#include <sys/sigprocmask.h>
#include <sys/sigaction.h>

#include <netlink.h>
#include <sigset.h>
#include <format.h>
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

#define NFDS 3

int rtnlfd;
int genlfd;
int ctrlfd;

struct pollfd pfds[NFDS];
static sigset_t defsigset;

int sigterm;
int sigchld;

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
		ta->tv_nsec = 1000000000 - tb->tv_nsec;
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
   However, if something goes wrong, the user gets a chance to interrept
   it with a second SIGTERM.

   At this point, NL requests are sent unchecked, and replies (if any)
   do not get read. Continuing with the main loop and waiting for the links
   to change state is quite complicated and completely pointless. */

static void stop_wait_procs(void)
{
	sigterm = 0;
	struct timespec ts = { 1, 0 };

	eprintf("%s\n", __FUNCTION__);

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

	setup_signals();
	setup_pollfds();

	while(!sigterm)
	{
		sigchld = 0;

		tp = poll_timeout(&ts, &te);

		int r = sysppoll(pfds, NFDS, tp, &defsigset);

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
