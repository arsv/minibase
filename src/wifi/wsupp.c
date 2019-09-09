#include <bits/socket/packet.h>
#include <bits/socket/unix.h>
#include <bits/ioctl/socket.h>
#include <bits/arp.h>
#include <bits/ether.h>

#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/socket.h>

#include <endian.h>
#include <string.h>
#include <sigset.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "wsupp.h"

ERRTAG("wsupp");

char** environ;

static sigset_t defsigset;
static struct pollfd pfds[3+NCONNS];
static int npfds;
static struct timespec pollts;
static callptr timercall;
int pollset;

static void sighandler(int sig)
{
	switch(sig) {
		case SIGCHLD:
			return check_script();
		case SIGHUP:
		case SIGINT:
		case SIGTERM:
			stop_wait_script();
			return exit_control();
	}
}

static void sigaction(int sig, struct sigaction* sa)
{
	int ret;

	if((ret = sys_sigaction(sig, sa, NULL)) < 0)
		fail("sigaction", NULL, ret);
}

static void setup_signals(void)
{
	SIGHANDLER(sa, sighandler, 0);

	sigaddset(&sa.mask, SIGINT);
	sigaddset(&sa.mask, SIGTERM);
	sigaddset(&sa.mask, SIGHUP);
	sigaddset(&sa.mask, SIGCHLD);

	sigaction(SIGINT,  &sa);
	sigaction(SIGTERM, &sa);
	sigaction(SIGHUP,  &sa);
	sigaction(SIGCHLD, &sa);
}

/* These do not get opened on startup. To avoid confusion with stdin,
   make sure they are all set to -1. */

static void clear_ondemand_fds(void)
{
	netlink = -1;
	rawsock = -1;
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

static void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	memzero(cn, sizeof(*cn));
	pollset = 0;
}

static void check_conn(struct pollfd* pf, struct conn* cn)
{
	if(pf->revents & POLLIN)
		handle_conn(cn);
	if(pf->revents & ~POLLIN)
		close_conn(cn);
}

static void check_netlink(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_netlink();
	if(pf->revents & ~POLLIN)
		quit("lost netlink connection", NULL, 0);
}

static void check_control(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_control();
	if(pf->revents & ~POLLIN)
		quit("lost control socket", NULL, 0);

	pollset = 0;
}

static void check_rawsock(struct pollfd* pf)
{
	if(pf->revents & POLLIN)
		handle_rawsock();
	if(!(pf->revents & ~POLLIN))
		return;

	sys_close(rawsock);
	rawsock = -1;
	pf->fd = -1;
}

static void update_pollfds(void)
{
	set_pollfd(&pfds[0], netlink);
	set_pollfd(&pfds[1], rawsock);
	set_pollfd(&pfds[2], ctrlfd);

	int i, n = 3;

	for(i = 0; i < nconns; i++)
		set_pollfd(&pfds[n+i], conns[i].fd);

	npfds = n + nconns;
	pollset = 1;
}

static void check_polled_fds(void)
{
	int i, n = 3;

	for(i = 0; i < nconns; i++)
		check_conn(&pfds[n+i], &conns[i]);

	check_netlink(&pfds[0]);
	check_rawsock(&pfds[1]);
	check_control(&pfds[2]);
}

void clear_timer(void)
{
	pollts.sec = 0;
	pollts.nsec = 0;
	timercall = NULL;
}

void set_timer(int seconds, callptr cb)
{
	pollts.sec = seconds;
	pollts.nsec = 0;
	timercall = cb;
}

int get_timer(void)
{
	return timercall ? pollts.sec : -1;
}

static void timer_expired(void)
{
	callptr cb = timercall;

	pollts.sec = 0;
	pollts.nsec = 0;
	timercall = NULL;

	if(cb) cb();
}

int main(int argc, char** argv)
{
	int i = 1, ret;

	if(i < argc)
		fail("too many arguments", NULL, 0);

	environ = argv + argc + 1;

	init_heap_ptrs();
	setup_signals();
	setup_control();
	clear_ondemand_fds();

	while(1) {
		struct timespec* ts = timercall ? &pollts : NULL;

		if(!pollset)
			update_pollfds();
		if((ret = sys_ppoll(pfds, npfds, ts, &defsigset)) > 0)
			check_polled_fds();
		else if(ret == 0)
			timer_expired();
		else if(ret != -EINTR)
			quit("ppoll", NULL, ret);
	};

	return 0; /* never reached */
}
