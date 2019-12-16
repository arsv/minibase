#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/ppoll.h>
#include <sys/signal.h>

#include <main.h>
#include <sigset.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"
#include "svchub.h"

struct proc procs[NPROCS];
struct conn conns[NCONNS];
static struct pollfd pfds[NPFDS];

static void check_signal(CTX)
{
	struct siginfo si;
	int ret, fd = ctx->sigfd;

	memzero(&si, sizeof(si));

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		quit(ctx, "read", "signalfd", ret);
	else if(ret == 0)
		quit(ctx, "signalfd", "EOF", 0);

	int sig = si.signo;

	if(sig == SIGCHLD)
		check_children(ctx);
	else if(sig == SIGPWR)
		signal_stop(ctx, "poweroff");
	else if(sig == SIGINT)
		signal_stop(ctx, "reboot");
	else if(sig == SIGTERM)
		signal_stop(ctx, "reboot");
}

static void setup_signals(CTX)
{
	int fd, ret;
	int flags = SFD_NONBLOCK | SFD_CLOEXEC;
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGPWR);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		quit(ctx, "signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		quit(ctx, "sigprocmask", NULL, ret);

	ctx->sigfd = fd;
}

static void start_procs(CTX)
{
	int ret;

	if((ret = reload_procs(ctx)) < 0)
		quit(ctx, NULL, INITDIR, ret);

	if(!ctx->active) terminate(ctx);
}

static void process_sigfd(CTX, struct pollfd* pf)
{
	int revents = pf->revents;

	if(revents & POLLIN)
		check_signal(ctx);
	if(revents & ~POLLIN)
		quit(ctx, "signalfd", "lost", 0);
}

static void process_ctlfd(CTX, struct pollfd* pf)
{
	int revents = pf->revents;

	if(revents & POLLIN)
		check_control(ctx);
	if(revents & ~POLLIN)
		quit(ctx, "control", "lost", 0);
}

static void process_proc(CTX, struct proc* rc, struct pollfd* pf)
{
	int revents = pf->revents;

	if(revents & POLLIN)
		check_proc(ctx, rc);
	if(revents & ~POLLIN)
		close_proc(ctx, rc);
}

static void process_conn(CTX, struct conn* cn, struct pollfd* pf)
{
	int revents = pf->revents;

	if(revents & POLLIN)
		check_conn(ctx, cn);
	if(revents & ~POLLIN)
		close_conn(ctx, cn);
}

static void check_polled_fds(CTX)
{
	int p = 2, n = ctx->npfds;

	int nprocs = ctx->nprocs;
	int nconns = ctx->nconns;
	int i, fd;

	for(i = 0; i < nprocs && p < n; i++)
		if((fd = procs[i].fd) >= 0)
			process_proc(ctx, &procs[i], &pfds[p++]);

	for(i = 0; i < nconns && p < n; i++)
		if((fd = conns[i].fd) >= 0)
			process_conn(ctx, &conns[i], &pfds[p++]);

	process_sigfd(ctx, &pfds[0]);
	process_ctlfd(ctx, &pfds[1]);
}

static void update_poll_fds(CTX)
{
	int p = 2, n = ARRAY_SIZE(pfds);

	int nprocs = ctx->nprocs;
	int nconns = ctx->nconns;
	int i, fd;

	if(ctx->pollset) return;

	for(i = 0; i < nprocs && p < n; i++)
		if((fd = procs[i].fd) >= 0)
			pfds[p++].fd = fd;

	for(i = 0; i < nconns && p < n; i++)
		if((fd = conns[i].fd) >= 0)
			pfds[p++].fd = fd;

	pfds[0].fd = ctx->sigfd;
	pfds[1].fd = ctx->ctlfd;

	for(i = 0; i < p; i++)
		pfds[i].events = POLLIN;

	ctx->pollset = 1;
	ctx->npfds = p;
}

static void poll_descriptors(CTX)
{
	int npfds = ctx->npfds;
	int ret;

	ctx->timeset = 0;

	if((ret = sys_ppoll(pfds, npfds, NULL, NULL)) < 0) {
		quit(ctx, "ppoll", NULL, ret);
	} else if(ret > 0) {
		check_polled_fds(ctx);
	}
}

static void wait_poll(CTX)
{
	update_poll_fds(ctx);

	poll_descriptors(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	
	memzero(ctx, sizeof(*ctx));

	if(argc > 1)
		quit(ctx, "extra arguments", NULL, 0);

	ctx->environ = argv + argc + 1;

	setup_control(ctx);
	setup_signals(ctx);
	start_procs(ctx);

	while(1) wait_poll(ctx);
}
