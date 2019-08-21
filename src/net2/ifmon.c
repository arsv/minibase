#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netlink.h>
#include <sigset.h>
#include <string.h>
#include <printf.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "ifmon.h"

ERRTAG("ifmon");

char** environ;
static int signalfd;

static void unlink_ctrl(void)
{
	sys_unlink(IFCTL);
}

void quit(const char* msg, char* arg, int err)
{
	unlink_ctrl();
	fail(msg, arg, err);
}

static void sighandler(int sig)
{
	switch(sig) {
		case SIGINT:
		case SIGTERM:
			unlink_ctrl();
			_exit(0xFF);
	}
}

static void setup_signals(void)
{
	int ret, fd;
	SIGHANDLER(sa, sighandler, 0);

	if((ret = sys_sigaction(SIGTERM, &sa, NULL)) < 0)
		quit("sigaction", "SIGTERM", ret);
	if((ret = sys_sigaction(SIGINT, &sa, NULL)) < 0)
		quit("sigaction", "SIGINT", ret);

	int flags = SFD_NONBLOCK | SFD_CLOEXEC;
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		quit("signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_BLOCK, &mask, NULL)) < 0)
		quit("sigprocmask", NULL, ret);

	signalfd = fd;
}

static void read_singals(void)
{
	struct siginfo si;
	int rd, fd = signalfd;

	if((rd = sys_read(fd, &si, sizeof(si))) < 0)
		quit("read", "sigfd", rd);
	else if(!rd)
		return;

	tracef("signal %i\n", si.signo);

	if(si.signo == SIGCHLD)
		got_sigchld();
}

static void set_pollfd(struct pollfd* pfd, int fd)
{
	pfd->fd = fd;
	pfd->events = POLLIN;
}

static int prepare_pollfds(struct pollfd* pfds, int maxpfds)
{
	int i, n = nconns;
	int p = 0;

	set_pollfd(&pfds[p++], rtnlfd);
	set_pollfd(&pfds[p++], ctrlfd);
	set_pollfd(&pfds[p++], signalfd);

	for(i = 0; i < n; i++)
		set_pollfd(&pfds[p++], conns[i].fd);

	return p;
}

static void check_netlink(int revents)
{
	if(revents & POLLIN)
		handle_rtnl();
	if(revents & ~POLLIN)
		quit("poll", "rtnl", 0);
}

static void check_control(int revents)
{
	if(revents & POLLIN)
		accept_ctrl();
	if(revents & ~POLLIN)
		quit("poll", "ctrl", 0);
}

static void check_signals(int revents)
{
	if(revents & POLLIN)
		read_singals();
	if(revents & ~POLLIN)
		quit("poll", "sigfd", 0);
}

static void close_conn(struct conn* cn)
{
	sys_close(cn->fd);
	free_conn_slot(cn);
}

static void check_conn(struct conn* cn, int revents)
{
	if(revents & POLLIN)
		handle_conn(cn);
	if(revents & ~POLLIN)
		close_conn(cn);
}

static struct conn* find_conn_for_fd(int fd, int* next)
{
	int i = *next;

	while(i < nconns) {
		struct conn* cn = &conns[i++];

		if(cn->fd == fd) {
			*next = i;
			return cn;
		}
	}

	*next = i;
	return NULL;
}

static void check_polled_fds(struct pollfd* pfds, int n)
{
	int p = 0, ic = 0;
	struct conn* cn;

	check_netlink(pfds[p++].revents);
	check_control(pfds[p++].revents);
	check_signals(pfds[p++].revents);

	while(p < n) {
		struct pollfd* pfd = &pfds[p++];

		if((cn = find_conn_for_fd(pfd->fd, &ic)))
			check_conn(cn, pfd->revents);
		else
			tracef("unhandled fd %i\n", pfd->fd);
	}

	check_links();
}

static void timed_out_waiting(void)
{

}

static void poll(void)
{
	struct pollfd pfds[4+NCONNS+NLINKS];
	int maxpfds = ARRAY_SIZE(pfds);
	int npfds = prepare_pollfds(pfds, maxpfds);

	int ret = sys_ppoll(pfds, npfds, NULL, NULL);

	if(ret < 0)
		quit("ppoll", NULL, ret);
	if(ret > 0)
		check_polled_fds(pfds, npfds);
	else
		timed_out_waiting();
}

int main(int argc, char** argv)
{
	if(argc > 1)
		fail("too many arguments", NULL, 0);

	environ = argv + argc + 1;

	setup_control();
	setup_netlink();
	setup_signals();

	while(1) poll();
}
