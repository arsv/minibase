#include <sys/file.h>
#include <sys/proc.h>
#include <sys/signal.h>

#include <string.h>
#include <format.h>
#include <sigset.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static char** prepenvp(CTX)
{
	if(ctx->customenvp < 0)
		return ctx->environ;

	struct env* ev;
	int i = 0, count = 0;
	char* var;

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev))
		if((var = env_value(ctx, ev, EV_ENVP)))
			count++;

	int size = (count+1)*sizeof(char*);
	char** envp = heap_alloc(ctx, size);

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev))
		if((var = env_value(ctx, ev, EV_ENVP)))
			if(i < count) envp[i++] = var;

	envp[i] = NULL;

	return envp;
}

static int child(CTX, char** argv, char** envp)
{
	struct sigset empty;
	int ret;

	if(ctx->sigfd < 0)
		goto exec;

	sigemptyset(&empty);

	if((ret = sys_sigprocmask(SIG_SETMASK, &empty, NULL)) < 0)
		error(ctx, "sigprocmask", NULL, ret);
exec:
	if((ret = execvpe(*argv, argv, envp)))
		error(ctx, "exec", *argv, ret);

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

	fatal(ctx, msg, buf);
}

/* It's tempting to re-use signalfd across several `run` commands,
   but for some reason it doesn't work. Messing with sigprocmask
   while signalfd is open breaks it somehow.

   Opening a new fd every time is a bit excessive, but it's simple
   and reliable.

   Note we cannot really open it here and only reset in cmd_exec()
   and cmd_invoke. At least one other command, cmd_sleep, relies
   on signals not being blocked. */

static int prep_signalfd(CTX)
{
	int fd, ret;
	int flags = SFD_CLOEXEC;
	struct sigset mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGHUP);
	sigaddset(&mask, SIGTERM);
	sigaddset(&mask, SIGCHLD);

	if((fd = sys_signalfd(-1, &mask, flags)) < 0)
		error(ctx, "signalfd", NULL, fd);

	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		error(ctx, "sigprocmask", NULL, ret);

	ctx->sigfd = fd;

	return fd;
}

static void close_signalfd(CTX, int fd)
{
	struct sigset empty;
	int ret;

	sigemptyset(&empty);

	if((ret = sys_sigprocmask(SIG_SETMASK, &empty, NULL)) < 0)
		error(ctx, "sigprocmask", NULL, ret);

	if((ret = sys_close(fd)) < 0)
		error(ctx, "close", "signalfd", ret);

	ctx->sigfd = -1;
}

static void wait_child(CTX, int fd, int pid, int* status)
{
	int ret;
	struct siginfo si;
again:
	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		error(ctx, "read", "signalfd", ret);

	int sig = si.signo;

	if(sig != SIGCHLD) {
		sys_kill(pid, sig);
		goto again;
	};

	if((ret = sys_waitpid(pid, status, WNOHANG)) > 0)
		return; /* got it */
	if(!ret || ret == -ECHILD)
		goto again;

	error(ctx, "waitpid", NULL, ret);
}

void cmd_run(CTX)
{
	int pid, status;

	need_some_arguments(ctx);

	char** argv = argsleft(ctx);
	char** envp = prepenvp(ctx);

	int fd = prep_signalfd(ctx);

	if((pid = sys_fork()) < 0)
		error(ctx, "fork", NULL, pid);

	if(pid == 0)
		_exit(child(ctx, argv, envp));

	wait_child(ctx, fd, pid, &status);

	close_signalfd(ctx, fd);

	if(status) describe(ctx, status);
}

void cmd_exec(CTX)
{
	need_some_arguments(ctx);

	char** argv = argsleft(ctx);
	char** envp = prepenvp(ctx);

	int ret = sys_execve(*argv, argv, envp);

	error(ctx, "exec", *argv, ret);
}

void cmd_invoke(CTX)
{
	int nargs = ctx->argc - ctx->argp;
	int norig = ctx->topargc - ctx->topargp;

	if(!nargs) error(ctx, "too few arguments", NULL, 0);

	char** envp = prepenvp(ctx);
	char** args = ctx->argv + ctx->argp;
	char** orig = ctx->topargv + ctx->topargp;

	char* argv[nargs + norig + 1];

	memcpy(argv, args, nargs*sizeof(char*));
	memcpy(argv + nargs, orig, norig*sizeof(char*));
	argv[nargs+norig] = NULL;

	int ret = sys_execve(*argv, argv, envp);

	error(ctx, "exec", *argv, ret);
}
