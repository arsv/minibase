#include <sys/file.h>
#include <sys/proc.h>
#include <sys/signal.h>

#include <string.h>
#include <format.h>
#include <sigset.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static int child(CTX, char** argv)
{
	struct sigset empty;
	int ret;

	if(ctx->sigfd < 0)
		goto exec;

	sigemptyset(&empty);

	if((ret = sys_sigprocmask(SIG_SETMASK, &empty, NULL)) < 0)
		return error(ctx, "sigprocmask", NULL, ret);
exec:
	if((ret = execvpe(*argv, argv, ctx->envp)))
		return error(ctx, "exec", *argv, ret);

	return 0xFF; /* should never happen */
}

static int describe(CTX, int status)
{
	char* msg;

	FMTBUF(p, e, buf, 20);

	if(WTERMSIG(status)) {
		msg = "command killed by signal";
		p = fmtint(p, e, WTERMSIG(status));
	} else {
		msg = "command failed with code";
		p = fmtint(p, e, WEXITSTATUS(status));
	};

	FMTEND(p, e);

	return error(ctx, msg, buf, 0);
}

static int prep_signalfd(CTX)
{
	int fd, ret;
	int flags = SFD_CLOEXEC;
	struct sigset mask;

	if((fd = ctx->sigfd) >= 0)
		goto set;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		return error(ctx, "signalfd", NULL, fd);

	ctx->sigfd = fd;
set:
	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		return error(ctx, "sigprocmask", NULL, ret);

	return fd;
}

static int wait_child(CTX, int fd, int pid, int* status)
{
	int ret;
	struct siginfo si;
	struct sigset empty;
read:
	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		return error(ctx, "read", "signalfd", ret);

	int sig = si.signo;

	if(sig != SIGCHLD) {
		sys_kill(pid, sig);
		goto read;
	};
wait:
	if((ret = sys_waitpid(-1, status, WNOHANG)) > 0) {
		if(ret == pid)
			goto done;
		else
			goto wait;
	} if(ret == -ECHILD) {
		goto read;
	} else {
		return error(ctx, "waitpid", NULL, ret);
	}
done:
	sigemptyset(&empty);

	if((ret = sys_sigprocmask(SIG_SETMASK, &empty, NULL)) < 0)
		return error(ctx, "sigprocmask", NULL, ret);

	return 0;
}

int cmd_run(CTX)
{
	int ret, fd, pid, status;

	if(noneleft(ctx))
		fatal(ctx, "missing command", NULL);

	char** argv = argsleft(ctx);

	if((fd = prep_signalfd(ctx)) < 0)
		return fd;

	if((pid = sys_fork()) < 0)
		return error(ctx, "fork", NULL, pid);

	if(pid == 0)
		_exit(child(ctx, argv));

	if((ret = wait_child(ctx, fd, pid, &status)) < 0)
		return ret;

	if(status)
		return describe(ctx, status);

	return 0;
}

int cmd_exec(CTX)
{
	if(noneleft(ctx))
		fatal(ctx, "missing command", NULL);

	char** argv = argsleft(ctx);

	return fchk(sys_execve(*argv, argv, ctx->envp), ctx, *argv);
}

int cmd_invoke(CTX)
{
	int nargs = numleft(ctx);
	int norig = ctx->topargc - ctx->topargp;

	if(!nargs)
		return error(ctx, "too few arguments", NULL, 0);

	char** args = ctx->argv + ctx->argp;
	char** orig = ctx->topargv + ctx->topargp;

	char* argv[nargs + norig + 1];

	memcpy(argv, args, nargs*sizeof(char*));
	memcpy(argv + nargs, orig, norig*sizeof(char*));
	argv[nargs+norig] = NULL;

	return fchk(sys_execve(*argv, argv, ctx->envp), ctx, *argv);
}
