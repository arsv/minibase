#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/signal.h>

#include <errtag.h>
#include <string.h>
#include <sigset.h>
#include <util.h>

#include "keymon.h"

ERRTAG("keymon");
ERRLIST(NEPERM NENOENT NENOTDIR NEACCES NENOTTY NEFAULT NEINVAL NEISDIR
	NELIBBAD NELOOP NEMFILE NENFILE NENOEXEC NENOMEM NETXTBSY);

#define PFDS (1 + NDEVICES)

static sigset_t defsigset;
struct pollfd pfds[PFDS];
int npfds, pfdkeys[PFDS];

char** environ;
int pollready;
int sigterm;

typedef struct timespec timespec;

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM: sigterm = 1; break;
	}
}

static void sigaction(int sig, struct sigaction* sa, char* tag)
{
	xchk(sys_sigaction(sig, sa, NULL), "sigaction", tag);
}

static void sigprocmask(int how, sigset_t* mask, sigset_t* out)
{
	xchk(sys_sigprocmask(how, mask, out), "sigiprocmask", "SIG_BLOCK");
}

void setup_signals(void)
{
	SIGHANDLER(sa, sighandler, SA_RESTART);

	sigemptyset(&sa.mask);
	sigprocmask(SIG_BLOCK, &sa.mask, &defsigset);

	sigaction(SIGINT,  &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGALRM, &sa, NULL);
}

static int add_poll_fd(int n, int fd, int key)
{
	if(fd <= 0)
		return n;

	struct pollfd* pf = &pfds[n];

	pf->fd = fd;
	pf->events = POLLIN;
	pfdkeys[n] = key;

	return n + 1;
}

void update_poll_fds(void)
{
	int i, n = 0;

	n = add_poll_fd(n, inotifyfd, 0);

	for(i = 0; i < ndevices; i++)
		n = add_poll_fd(n, devices[i].fd, 1 + i);

	npfds = n;
	pollready = 1;
}

/* Failing fds are dealt with immediately, to avoid re-queueing
   them from ppoll. For keyboards, this is also the only place
   where the fds get closed. Control fds are closed either here
   or in closevt(). */

static void close_device(struct device* kb)
{
	sys_close(kb->fd);
	memzero(kb, sizeof(kb));
	pollready = 0;
}

static void recv_device(struct pollfd* pf, struct device* kb)
{
	if(pf->revents & POLLIN)
		handle_input(kb, pf->fd);
	if(pf->revents & ~POLLIN)
		close_device(kb);
}

static void recv_inotify(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_inotify(pf->fd);
	if(pf->revents & ~POLLIN)
		fail("inotify fd gone", NULL, 0);
}


void check_polled_fds(void)
{
	int i, key;

	for(i = 0; i < npfds; i++)
		if((key = pfdkeys[i]) == 0)
			recv_inotify(&pfds[i]);
		else if(key > 0)
			recv_device(&pfds[i], &devices[key-1]);
}

static timespec* prep_poll_time(timespec* ts0, timespec* ts1)
{
	struct action* ka;
	int timetowait = 0;

	for(ka = actions; ka < actions + nactions; ka++)
		if(!ka->time)
			continue;
		else if(!timetowait || ka->time < timetowait)
			timetowait = ka->time;

	if(timetowait <= 0)
		return NULL;

	ts0->sec = timetowait / 1000;
	ts0->nsec = (timetowait % 1000) * 1000000;

	*ts1 = *ts0;

	return ts1;
}

static void update_timers(timespec* ts0, timespec* ts1)
{
	long wait = ts0->sec*1000 + ts0->nsec/1000000;
	long left = ts1->sec*1000 + ts1->nsec/1000000;
	long diff = wait - left;

	if(diff <= 0)
		return;

	struct action* ka;

	for(ka = actions; ka < actions + nactions; ka++)
		if(!ka->time)
			continue;
		else if(ka->time <= diff)
			hold_done(ka);
		else
			ka->time -= diff;
}

int main(int argc, char** argv, char** envp)
{
	(void)argv;

	int ret;
	timespec ts0, ts1, *pts;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	environ = envp;

	load_config();
	setup_signals();
	setup_devices();

	while(!sigterm) {
		if(!pollready)
			update_poll_fds();

		pts = prep_poll_time(&ts0, &ts1);

		ret = sys_ppoll(pfds, npfds, pts, &defsigset);

		if(ret < 0 && ret != -EINTR)
			fail("ppoll", NULL, ret);
		else if(ret > 0)
			check_polled_fds();
		if(pts)
			update_timers(&ts0, &ts1);

	}

	return 0;
}
