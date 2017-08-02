#include <sys/signal.h>
#include <sys/file.h>
#include <sys/poll.h>

#include <string.h>
#include <sigset.h>
#include <null.h>
#include <fail.h>

#include "keymon.h"

ERRTAG = "keymon";
ERRLIST = { RESTASNUMBERS };

#define PFDS (1 + NDEVICES)

static sigset_t defsigset;
struct pollfd pfds[PFDS];
int npfds, pfdkeys[PFDS];

char** environ;
int pollready;
int sigterm;

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
	struct sigaction sa = {
		.handler = sighandler,
		.flags = SA_RESTART | SA_RESTORER,
		.restorer = sigreturn
	};

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

int main(int argc, char** argv, char** envp)
{
	int ret;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	environ = envp;

	load_config();
	setup_signals();
	setup_devices();

	while(!sigterm) {
		if(!pollready)
			update_poll_fds();

		ret = sys_ppoll(pfds, npfds, NULL, &defsigset);

		if(ret < 0)
			fail("ppoll", NULL, ret);
		else if(ret > 0)
			check_polled_fds();
	}

	return 0;
}
