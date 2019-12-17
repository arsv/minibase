#include <bits/socket/unix.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/time.h>

#include <netlink.h>
#include <sigset.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "ifmon.h"

ERRTAG("ifmon");

static void setup_signals(CTX)
{
	int ret, fd;
	int flags = SFD_NONBLOCK | SFD_CLOEXEC;
	struct sigset mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		fail("signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_BLOCK, &mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);

	ctx->signalfd = fd;
}

static void read_singals(CTX)
{
	struct siginfo si;
	int rd, fd = ctx->signalfd;

	if((rd = sys_read(fd, &si, sizeof(si))) < 0)
		fail("read", "sigfd", rd);
	else if(!rd)
		return;

	if(si.signo == SIGCHLD)
		got_sigchld(ctx);
}

static void set_pollfd(struct pollfd* pfd, int fd)
{
	pfd->fd = fd;
	pfd->events = POLLIN;
}

static int prepare_pollfds(CTX, struct pollfd* pfds, int maxpfds)
{
	struct conn* conns = ctx->conns;
	int nconns = ctx->nconns;
	int i, p = 0;
	int fd;

	set_pollfd(&pfds[p++], ctx->rtnlfd);
	set_pollfd(&pfds[p++], ctx->ctrlfd);
	set_pollfd(&pfds[p++], ctx->signalfd);

	for(i = 0; i < nconns; i++)
		if((fd = conns[i].fd) >= 0)
			set_pollfd(&pfds[p++], fd);

	return p;
}

static void check_netlink(CTX, int revents)
{
	if(revents & POLLIN)
		handle_rtnl(ctx);
	if(revents & ~POLLIN)
		fail("poll", "rtnl", 0);
}

static void check_control(CTX, int revents)
{
	if(revents & POLLIN)
		accept_ctrl(ctx);
	if(revents & ~POLLIN)
		fail("poll", "ctrl", 0);
}

static void check_signals(CTX, int revents)
{
	if(revents & POLLIN)
		read_singals(ctx);
	if(revents & ~POLLIN)
		fail("poll", "sigfd", 0);
}

static void check_conn(CTX, struct conn* cn, int revents)
{
	if(revents & POLLIN)
		handle_conn(ctx, cn);
	if(revents & ~POLLIN)
		close_conn(ctx, cn);
}

static struct conn* find_conn_for_fd(CTX, int fd, int* next)
{
	int i = *next;
	struct conn* conns = ctx->conns;
	int nconns = ctx->nconns;

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

static void check_polled_fds(CTX, struct pollfd* pfds, int n)
{
	int p = 0, ic = 0;
	struct conn* cn;

	check_netlink(ctx, pfds[p++].revents);
	check_control(ctx, pfds[p++].revents);
	check_signals(ctx, pfds[p++].revents);

	while(p < n) {
		struct pollfd* pfd = &pfds[p++];
		if((cn = find_conn_for_fd(ctx, pfd->fd, &ic)))
			check_conn(ctx, cn, pfd->revents);
	}
}

static void poll(CTX)
{
	struct pollfd pfds[4+NCONNS];
	int maxpfds = ARRAY_SIZE(pfds);
	int npfds = prepare_pollfds(ctx, pfds, maxpfds);

	int ret = sys_ppoll(pfds, npfds, NULL, NULL);

	if(ret < 0)
		fail("ppoll", NULL, ret);
	if(ret > 0)
		check_polled_fds(ctx, pfds, npfds);

	check_links(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));
	ctx->environ = argv + argc + 1;

	setup_control(ctx);
	setup_netlink(ctx);
	setup_signals(ctx);

	while(1) poll(ctx);
}
