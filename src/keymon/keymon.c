#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/signal.h>
#include <sys/prctl.h>

#include <string.h>
#include <sigset.h>
#include <util.h>
#include <main.h>

#include "keymon.h"

ERRTAG("keymon");

int find_device_slot(CTX)
{
	struct pollfd* pfds = ctx->pfds;
	int i, npfds = ctx->npfds;

	for(i = FDX; i < npfds; i++)
		if(pfds[i].fd < 0)
			goto got;
	if(i >= NPFDS)
		return -1;
got:
	return i - FDX;
}

void set_static_fd(CTX, int i, int fd)
{
	struct pollfd* pfds = ctx->pfds;
	struct pollfd* pf = &pfds[i];

	pf->fd = fd;
	pf->events = POLLIN;

	if(i < ctx->npfds)
		return;

	int npfds = i + 1;

	ctx->npfds = i + 1;
	ctx->nbits = npfds > 2 ? npfds - 2 : 0;
}

void set_device_fd(CTX, int i, int fd)
{
	set_static_fd(ctx, i + FDX, fd);
}

static void update_npfds(CTX)
{
	struct pollfd* pfds = ctx->pfds;
	int n = ctx->npfds;

	while(n > FDX) {
		struct pollfd* pf = &pfds[n-1];

		if(pf->fd >= 0) break;

		n--;
	}

	ctx->npfds = n;
}

static void handle_signals(CTX, int fd)
{
	struct siginfo si;
	int ret;

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		fail("read", "sigfd", ret);
	if(ret == 0)
		fail("signalfd EOF", NULL, 0);

	int sig = si.signo;

	if(sig != SIGCHLD)
		return;

	check_children(ctx);
}

static void close_device(CTX, struct pollfd* pf, byte* mods)
{
	sys_close(pf->fd);

	pf->fd = -1;
	pf->events = 0;

	*mods = 0;

	update_npfds(ctx);
}

static void check_device(CTX, struct pollfd* pf, byte* mods)
{
	int revents = pf->revents;

	if(revents & POLLIN)
		handle_input(ctx, pf->fd, mods);
	if(revents & ~POLLIN)
		close_device(ctx, pf, mods);
}

static void check_signals(CTX, struct pollfd* pf)
{
	int revents = pf->revents;

	if(revents & ~POLLIN)
		fail("lost", "signalfd", 0);
	if(revents & POLLIN)
		handle_signals(ctx, pf->fd);
}

static void check_inotify(CTX, struct pollfd* pf)
{
	int revents = pf->revents;

	if(revents & ~POLLIN)
		fail("lost", "inotify", 0);
	if(revents & POLLIN)
		handle_inotify(ctx, pf->fd);
}

static void check_polled_fds(CTX)
{
	struct pollfd* pfds = ctx->pfds;
	int i, npfds = ctx->npfds;
	byte* bits = ctx->bits;

	for(i = FDX; i < npfds; i++)
		check_device(ctx, &pfds[i], &bits[i-FDX]);

	check_inotify(ctx, &pfds[1]);
	check_signals(ctx, &pfds[0]);
}

static void poll(CTX)
{
	struct pollfd* pfds = ctx->pfds;
	int npfds = ctx->npfds;
	struct act* held = ctx->held;
	struct timespec* ts = held ? &ctx->ts : NULL;

	int ret = sys_ppoll(pfds, npfds, ts, NULL);

	if(ret == 0)
		hold_timeout(ctx, held);
	else if(ret > 0)
		check_polled_fds(ctx);
	else if(ret != -EINTR)
		fail("ppoll", NULL, ret);
}

static void open_signals(CTX)
{
	int fd, ret;
	struct sigset mask;
	int flags = SFD_NONBLOCK | SFD_CLOEXEC;

	sigemptyset(&mask);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		fail("signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);

	set_static_fd(ctx, 0, fd);
}

static void set_subreaper(void)
{
	int ret;

	if((ret = sys_prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0)) < 0)
		fail("prctl", "PR_SET_CHILD_SUBREAPER", ret);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	struct pollfd pfds[NPFDS];
	byte bits[NDEVS];
	byte acts[ACLEN];

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	ctx->environ = argv + argc + 1;
	ctx->bits = bits;
	ctx->acts = acts;
	ctx->pfds = pfds;
	ctx->npfds = 2;
	ctx->nbits = 0;

	load_config(ctx);
	open_signals(ctx);
	scan_devices(ctx);
	set_subreaper();

	while(1) poll(ctx);
}
