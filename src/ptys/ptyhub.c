#include <bits/socket/unix.h>

#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ppoll.h>
#include <sys/socket.h>
#include <sys/timer.h>
#include <sys/signal.h>
#include <sys/prctl.h>

#include <string.h>
#include <sigset.h>
#include <printf.h>
#include <main.h>
#include <util.h>

#include "common.h"
#include "ptyhub.h"

ERRTAG("ptyhub");

static void kill_all_procs(CTX)
{
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;
	int pid;

	for(; pc < pe; pc++) {
		if((pid = pc->pid) <= 0)
			continue;

		(void)sys_kill(pid, SIGTERM);
	}
}

static void kill_all_conns(CTX)
{
	struct conn* cn = ctx->conns;
	struct conn* ce = cn + ctx->nconns;
	int fd;

	for(; cn < ce; cn++) {
		if((fd = cn->fd) <= 0)
			continue;

		sys_close(fd);
		cn->fd = -1;
	}

	sys_close(ctx->ctlfd);
	ctx->ctlfd = -1;

	ctx->pollset = 0;
}

static void sigprocmask(int how, struct sigset* mask)
{
	int ret;

	if((ret = sys_sigprocmask(how, mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);
}

static void open_signalfd(CTX)
{
	int fd;
	struct sigset ss;

	sigemptyset(&ss);

	sigaddset(&ss, SIGINT);
	sigaddset(&ss, SIGHUP);
	sigaddset(&ss, SIGTERM);
	sigaddset(&ss, SIGCHLD);

	sigprocmask(SIG_BLOCK, &ss);

	if((fd = sys_signalfd(-1, &ss, SFD_CLOEXEC)) < 0)
		fail("signalfd", NULL, fd);

	ctx->sigfd = fd;
}

static void clear_exit(CTX)
{
	_exit(0xFF);
}

static void maybe_normal_exit(CTX)
{
	if(ctx->timer != TM_STOP)
		return;
	if(ctx->nprocs_running)
		return;

	clear_exit(ctx);
}

static void request_stop(CTX, const char* signame)
{
	if(ctx->timer == TM_STOP) {
		warn(signame, NULL, 0);
		clear_exit(ctx);
	}

	kill_all_procs(ctx);
	kill_all_conns(ctx);

	ctx->timer = TM_STOP;
	ctx->ts.sec = 5;
	ctx->ts.nsec = 0;

	maybe_normal_exit(ctx);
}

static void check_signal(CTX)
{
	struct siginfo si;
	int fd = ctx->sigfd;
	int ret;

	memzero(&si, sizeof(si));

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		fail("read", "sigfd", ret);
	if(ret == 0)
		fail("signalfd EOF", NULL, 0);

	int sig = si.signo;

	if(sig == SIGCHLD) {
		check_children(ctx);
		maybe_normal_exit(ctx);
	} else if(sig == SIGINT) {
		request_stop(ctx, "SIGINT");
	} else if(sig == SIGTERM) {
		request_stop(ctx, "SIGTERM");
	} else if(sig == SIGHUP) {
		request_stop(ctx, "SIGHUP");
	}
}

static void handle_timeout(CTX)
{
	int timer = ctx->timer;

	ctx->timer = TM_NONE;

	if(timer == TM_MMAP)
		maybe_drop_iobuf(ctx);
	if(timer == TM_STOP)
		clear_exit(ctx);
}

static void reset_pollfds(CTX)
{
	int npfds = 2 + ctx->nconns_active + 2*ctx->nprocs_nonempty;
	struct pollfd* pfds;
	int need = npfds*sizeof(*pfds);
	void* buf;
	int len;

	ctx->npfds = 0;
	ctx->ptr = ctx->sep;

	if(need < ssizeof(ctx->pollbuf))
		goto pbuf;

	if(!(buf = heap_alloc(ctx, need)))
		goto pbuf;

	len = need;
	goto done;
pbuf:
	buf = ctx->pollbuf;
	len = sizeof(ctx->pollbuf);
done:
	ctx->pfds = buf;
	ctx->pfde = buf + len;
}

static void add_poll_fd(CTX, int fd)
{
	int n = ctx->npfds;
	struct pollfd* pfd = &ctx->pfds[n];
	struct pollfd* pfe = ctx->pfde;

	if(pfd + 1 > pfe) {
		ctx->pollset = 0;
		warn("pfds overflow", NULL, 0);
		return;
	}

	pfd->fd = fd;
	pfd->events = POLLIN;

	ctx->npfds = n + 1;
}

static void add_conn_fds(CTX)
{
	struct conn* cn = ctx->conns;
	struct conn* ce = cn + ctx->nconns;
	int fd;

	for(; cn < ce; cn++)
		if((fd = cn->fd) > 0)
			add_poll_fd(ctx, fd);

	ctx->npsep = ctx->npfds;
}

static void add_proc_fds(CTX)
{
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;

	for(; pc < pe; pc++) {
		if(!pc->xid)
			continue;

		int cfd = pc->cfd;
		int mfd = pc->mfd;
		int efd = pc->efd;

		if(mfd > 0 && cfd < 0)
			add_poll_fd(ctx, mfd);
		if(efd > 0)
			add_poll_fd(ctx, efd);
	}
}

static void update_poll_fds(CTX)
{
	reset_pollfds(ctx);

	ctx->pollset = 1;

	add_poll_fd(ctx, ctx->ctlfd);
	add_poll_fd(ctx, ctx->sigfd);

	add_conn_fds(ctx);
	add_proc_fds(ctx);

	maybe_trim_heap(ctx);
}

static void process_conns(CTX)
{
	struct pollfd* pfds = ctx->pfds;
	int i = 2, n = ctx->npsep;
	struct conn* cn = ctx->conns;
	struct conn* ce = cn + ctx->nconns;

	for(; i < n; i++) {
		struct pollfd* pf = &pfds[i];
		int revents = pf->revents;
		int polledfd = pf->fd;

		if(!revents)
			continue;

		for(; cn < ce; cn++)
			if(cn->fd == polledfd)
				break;
		if(cn >= ce)
			break;

		if(revents & POLLIN)
			handle_conn(ctx, cn);
		if(revents & ~POLLIN)
			close_conn(ctx, cn);

	} if(i < n) {
		ctx->pollset = 0;
	}
}

static void process_proc_efds(CTX)
{
	struct pollfd* pfds = ctx->pfds;
	int i = ctx->npsep, n = ctx->npfds;
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;

	if(!pc) return;

	for(; i < n; i++) {
		struct pollfd* pf = &pfds[i];
		int revents = pf->revents;
		int polledfd = pf->fd;

		if(!revents)
			continue;

		for(; pc < pe; pc++)
			if(pc->efd == polledfd)
				break;
		if(pc >= pe)
			break;

		if(revents & POLLIN)
			handle_stderr(ctx, pc);
		if(revents & ~POLLIN)
			close_stderr(ctx, pc);

	} if(i < n) {
		ctx->pollset = 0;
	}
}

static void process_proc_mfds(CTX)
{
	struct pollfd* pfds = ctx->pfds;
	int i = ctx->npsep, n = ctx->npfds;
	struct proc* pc = ctx->procs;
	struct proc* pe = pc + ctx->nprocs;

	if(!pc) return;

	for(; i < n; i++) {
		struct pollfd* pf = &pfds[i];
		int revents = pf->revents;
		int polledfd = pf->fd;

		if(!revents)
			continue;

		for(; pc < pe; pc++)
			if(pc->mfd == polledfd)
				break;
		if(pc >= pe)
			break;

		if(revents & POLLIN)
			handle_stdout(ctx, pc);
		if(revents & ~POLLIN)
			close_stdout(ctx, pc);

	} if(i < n) {
		ctx->pollset = 0;
	}
}

static void process_events(CTX)
{
	struct pollfd* pfds = ctx->pfds;

	if(pfds[0].revents & POLLIN)
		check_socket(ctx);
	if(pfds[1].revents & POLLIN)
		check_signal(ctx);

	/* important! handle proc input *before* accepting commands,
	   otherwise there may be a race between local flush and a client
	   reading from the fds it got in response to CMD_ATTACH. */
	process_proc_mfds(ctx);
	process_proc_efds(ctx);

	process_conns(ctx);
}

static void poll(CTX)
{
	int ret;

	if(!ctx->pollset)
		update_poll_fds(ctx);

	struct pollfd* pfds = ctx->pfds;
	int npfds = ctx->npfds;
	struct timespec* ts = &ctx->ts;

	if(ctx->timer == TM_NONE) ts = NULL;

	if((ret = sys_ppoll(pfds, npfds, ts, NULL)) < 0)
		fail("ppoll", NULL, ret);

	if(ret)
		process_events(ctx);
	else
		handle_timeout(ctx);
}

static void set_subreaper(void)
{
	int ret;

	if((ret = sys_prctl(PR_SET_CHILD_SUBREAPER, 1, 0, 0, 0)) < 0)
		fail("prctl", "PR_SET_CHILD_SUBREAPER", ret);
}

int main(int argc, char** argv)
{
	(void)argv;
	struct top context, *ctx = &context;

	if(argc > 1)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	set_subreaper();

	open_signalfd(ctx);
	setup_control(ctx);

	while(1) poll(ctx);
}
