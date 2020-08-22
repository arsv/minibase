#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/timer.h>

#include <string.h>
#include <sigset.h>
#include <nlusctl.h>
#include <main.h>
#include <util.h>

#include "timed.h"
#include "common.h"

ERRTAG("timed");

struct poll {
	struct pollfd pfds[NPOLLS];
	int npfds;
};

struct serv* current(CTX)
{
	int i = ctx->current;

	if(i < 0)
		quit("no current server", NULL, 0);
	if(i >= NSERVS)
		quit("server idx out of range", NULL, 0);

	struct serv* sv = &ctx->servs[i];

	if(!(sv->flags & SF_SET))
		quit("invalid server idx", NULL, 0);

	return sv;
}

void quit(const char* msg, char* arg, int err)
{
	fail(msg, arg, err);
}

static void nilhandler(int sig)
{
	/* we only need it to let ppoll exit with -EINTR */
	(void)sig;
}

static void setup_signals(CTX)
{
	struct sigset set;
	int ret;

	SIGHANDLER(sa, nilhandler, 0);

	if((ret = sys_sigaction(SIGALRM, &sa, NULL)) < 0)
		fail("sigaction", "SIGALRM", ret);

	sigemptyset(&set);
	sigaddset(&set, SIGALRM);

	if((ret = sys_sigprocmask(SIG_SETMASK, &set, NULL)) < 0)
		fail("sigprocmask", NULL, ret);
}

static void setup_control(CTX)
{
	int fd, ret;
	int flags = SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC;
	char* path = CONTROL;

	if((fd = sys_socket(AF_UNIX, flags, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = uc_listen(fd, path, 5)) < 0)
		fail("ucbind", path, ret);

	ctx->ctlfd = fd;
	ctx->ntpfd = -1;
}

static void setup_pollfds(CTX, struct poll* pp)
{
	struct pollfd* pf;

	for(pf = pp->pfds; pf < ARRAY_END(pp->pfds); pf++)
		pf->events = POLLIN;
}

static int ifgoodfd(int fd)
{
	return (fd > 0 ? fd : -1);
}

static void update_poll_fds(CTX, struct poll* pp)
{
	int i, n = ctx->nconn;
	struct pollfd* pfds = pp->pfds;

	pfds[0].fd = ctx->ctlfd;
	pfds[1].fd = ifgoodfd(ctx->ntpfd);

	for(i = 0; i < n; i++)
		pfds[2+i].fd = ifgoodfd(ctx->conns[i].fd);

	pp->npfds = 2 + n;
}

void clear_client(CTX, CN)
{
	int i, n = 0;

	sys_close(cn->fd);
	cn->fd = -1;

	for(i = 0; i < NCONNS; i++)
		if(ctx->conns[i].fd > 0)
			n = i + 1;

	ctx->nconn = n;
}

static struct conn* grab_conn_slot(CTX)
{
	int i, n = NCONNS;

	for(i = 0; i < n; i++) {
		struct conn* cn = &ctx->conns[i];

		if(cn->fd > 0)
			continue;

		if(i >= ctx->nconn)
			ctx->nconn = i + 1;

		return cn;
	}

	return NULL;
}

static void check_control(CTX)
{
	int fd, sfd = ctx->ctlfd;
	int flags = SOCK_NONBLOCK;
	struct sockaddr addr;
	int addr_len = sizeof(addr);
	struct conn* cn;

	while((fd = sys_accept4(sfd, &addr, &addr_len, flags)) > 0) {
		if(!(cn = grab_conn_slot(ctx))) {
			sys_close(fd);
		} else {
			cn->fd = fd;
			ctx->pollready = 0;
		}
	}
}

static void check_polled_fds(CTX, struct poll* pp)
{
	struct pollfd* pfds = pp->pfds;
	int i, n = ctx->nconn;

	if(pfds[0].revents & POLLIN)
		check_control(ctx);
	if(pfds[1].revents & POLLIN)
		check_packet(ctx);

	for(i = 0; i < n; i++) {
		int revents = pfds[2+i].revents;

		if(revents & POLLIN)
			check_client(ctx, &ctx->conns[i]);
		if(revents & ~POLLIN)
			clear_client(ctx, &ctx->conns[i]);
	}
}

static struct timespec* wait_timeout(CTX)
{
	int state = ctx->state;

	if(state == TS_IDLE)
		return NULL;
	if(ctx->tistate == TI_ARMED)
		return NULL;

	return &ctx->alarm;
}

static void start_poll_timer(CTX, int sec)
{
	int ret;
	struct itimerspec its = {
		.interval = { 0, 0 },
		.value = { sec + 10, 0 }
	};
	struct sigevent sevt = {
		.notify = SIGEV_SIGNAL,
		.signo = SIGALRM
	};

	if(ctx->tistate != TI_NONE)
		goto arm;

	if((ret = sys_timer_create(CLOCK_BOOTTIME, &sevt, &ctx->timerid)) < 0) {
		warn("timer_create", NULL, ret);
		return;
	}

	ctx->tistate = TI_CLEAR;
arm:
	if((ret = sys_timer_settime(ctx->timerid, 0, &its, NULL)) < 0) {
		warn("timer_settime", NULL, ret);
		return;
	}

	ctx->tistate = TI_ARMED;

}

static void clear_poll_timer(CTX)
{
	int ret;
	struct itimerspec its = {
		{ 0, 0 },
		{ 0, 0 }
	};

	if(ctx->tistate != TI_ARMED)
		return;

	if((ret = sys_timer_settime(ctx->timerid, 0, &its, NULL)) < 0)
		warn("timer_settime", NULL, ret);

	ctx->tistate = TI_CLEAR;
}

static void delete_poll_timer(CTX)
{
	int ret;

	if(ctx->tistate == TI_NONE)
		return;

	if((ret = sys_timer_delete(ctx->timerid)) < 0)
		warn("timer_delete", NULL, ret);

	ctx->tistate = TI_NONE;
}

void stop_service(CTX)
{
	ctx->state = TS_IDLE;
	ctx->current = -1;
	ctx->failures = 0;
	ctx->interval = 0;

	delete_poll_timer(ctx);
}

void set_timed(CTX, int state, int sec)
{
	ctx->state = state;

	if(state == TS_POLL_WAIT && sec > 5*60) {
		start_poll_timer(ctx, sec);

		ctx->alarm.sec = 0;
		ctx->alarm.nsec = -1;
	} else {
		clear_poll_timer(ctx);

		ctx->alarm.sec = sec;
		ctx->alarm.nsec = 0;
	}
}

static void timed_out_waiting(CTX)
{
	ctx->alarm.nsec = -1;

	handle_timeout(ctx);
}

static void sleep_timer_expired(CTX)
{
	if(ctx->tistate != TI_ARMED)
		return; /* stray signal? */

	ctx->tistate = TI_CLEAR;

	handle_timeout(ctx);
}

int main(int argc, char** argv)
{
	int ret;
	(void)argv;
	struct top context, *ctx = &context;
	struct poll pollctx, *pp = &pollctx;
	struct sigset empty;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));
	memzero(pp, sizeof(*pp));
	sigemptyset(&empty);

	ctx->current = -1;

	init_clock_state(ctx);

	setup_signals(ctx);
	setup_control(ctx);
	setup_pollfds(ctx, pp);

	while(1) {
		struct timespec* ts = wait_timeout(ctx);

		update_poll_fds(ctx, pp);

		if((ret = sys_ppoll(pp->pfds, pp->npfds, ts, &empty)) > 0)
			check_polled_fds(ctx, pp);
		else if(ret == -EINTR)
			sleep_timer_expired(ctx);
		else if(ret < 0)
			quit("ppoll", NULL, ret);
		else
			timed_out_waiting(ctx);
	}
}
