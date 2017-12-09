#include <sys/file.h>
#include <sys/signal.h>
#include <sys/ppoll.h>
#include <sys/mman.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <sigset.h>
#include <util.h>

#include "cmd.h"
#include "unicode.h"

ERRTAG("cmd");

noreturn void exit(CTX, int code)
{
	fini_input(ctx);
	_exit(code);
}

noreturn void quit(CTX, const char* msg, char* arg, int err)
{
	fini_input(ctx);
	fail(msg, arg, err);
}

static void sigprocmask(int how, sigset_t* mask)
{
	int ret;

	if((ret = sys_sigprocmask(how, mask, NULL)) < 0)
		fail("sigprocmask", NULL, ret);
}

static void setup_signals(CTX)
{
	sigset_t mask;
	int fd;

	sigemptyset(&mask);
	sigaddset(&mask, SIGWINCH);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGQUIT);
	sigaddset(&mask, SIGCHLD);

	sigprocmask(SIG_BLOCK, &mask);

	if((fd = sys_signalfd(-1, &mask, SFD_CLOEXEC)) < 0)
		fail("signalfd", NULL, fd);

	ctx->sigfd = fd;
}

static void setup_buffers(CTX)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + PAGE);

	if(brk_error(brk, end))
		fail("cannot initialize heap", NULL, 0);

	ctx->heap.brk = brk;
	ctx->heap.ptr = brk;
	ctx->heap.end = end;

	char* ptr = brk;

	ctx->out.buf = ptr;
	ctx->out.ptr = 0;
	ctx->out.len = 512;
	ptr += 512;

	ctx->buf = ptr;
	ctx->sep = 0;
	ctx->ptr = 0;
	ctx->max = 1024;
	ptr += 1024;
}

static void setup(CTX, int argc, char** argv, char** envp)
{
	ctx->argc = argc;
	ctx->argv = argv;
	ctx->envp = envp;

	setup_signals(ctx);
	setup_buffers(ctx);
}

static void recv_sigfd(CTX)
{
	int fd = ctx->sigfd;
	struct sigevent se;
	int rd;

	while((rd = sys_read(fd, &se, sizeof(se))) > 0) {
		if(rd < sizeof(se))
			quit(ctx, "bad sigevent size", NULL, rd);

		switch(se.signo) {
			case SIGWINCH: update_winsz(ctx); break;
			case SIGINT:
			case SIGTERM: exit(ctx, 0xFF);
		}
	}

	if(rd == -EAGAIN)
		return;
	else if(rd < 0) /* should never happen */
		quit(ctx, "read", "signalfd", rd);
}

static void recv_stdin(CTX, char* buf, int* offp, int len)
{
	int off = *offp;
	int rd;

	if((rd = sys_read(STDIN, buf + off, len - off)) > 0)
		;
	else if(rd == -EINTR)
		return;
	else if(rd == 0)
		return;
	else
		quit(ctx, NULL, "stdin", rd);

	int total = off + rd;
	int parsed = handle_input(ctx, buf, total);

	if(parsed < 0) /* should never happen */
		quit(ctx, "parser error", NULL, 0);
	if(parsed < total) {
		memmove(buf, buf + parsed, off);
		*offp = total - parsed;
	} else {
		*offp = 0;
	};
}

static void input_loop(CTX)
{
	struct pollfd pfds[2] = {
		{ .fd = STDIN,      .events = POLLIN },
		{ .fd = ctx->sigfd, .events = POLLIN }
	};
	char inbuf[50];
	int off = 0, len = sizeof(inbuf);
	int ret;

	while(1) {
		if((ret = sys_ppoll(pfds, 2, NULL, NULL)) > 0)
			;
		else if(ret == -EINTR)
			continue;
		else if(ret < 0)
			quit(ctx, "poll", NULL, ret);

		if(pfds[0].revents & POLLIN)
			recv_stdin(ctx, inbuf, &off, len);
		if(pfds[0].revents & ~POLLIN)
			return; /* EOF on stdin */

		if(pfds[1].revents & POLLIN)
			recv_sigfd(ctx);
		if(pfds[1].revents & ~POLLIN)
			quit(ctx, "signalfd lost", NULL, 0);
	}
}

int main(int argc, char** argv, char** envp)
{
	struct top context, *ctx = &context;
	memzero(ctx, sizeof(*ctx));

	setup(ctx, argc, argv, envp);
	prep_prompt(ctx);

	init_input(ctx);
	input_loop(ctx);
	fini_input(ctx);

	return 0;
}
