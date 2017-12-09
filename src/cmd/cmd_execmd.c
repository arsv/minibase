#include <sys/proc.h>
#include <sys/file.h>
#include <sys/fprop.h>
#include <sys/fpath.h>
#include <sys/prctl.h>
#include <sys/signal.h>

#include <sigset.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "cmd.h"

struct builtin;

static struct builtin* find_builtin(char* name)
{
	return NULL;
}

static void runbi(CTX, const struct builtin* fn, int argc, char** argv)
{

}

static int trywaitpid(int pid, int* status)
{
	int ret;

	if((ret = sys_waitpid(-1, status, WNOHANG)) < 0) {
		if(ret != -EAGAIN)
			warn("wait", NULL, ret);
		return ret;
	}

	if(ret != pid) /* stray child */
		return -ESRCH;

	return ret;
}

static int child(char* exe, char** argv, char** envp)
{
	sigset_t mask;

	sigemptyset(&mask);
	sys_sigprocmask(SIG_SETMASK, &mask, NULL);
	sys_prctl(PR_SET_PDEATHSIG, SIGTERM, 0, 0, 0);

	int ret = execvpe(exe, argv, envp);

	fail(NULL, exe, ret);
}

static void spawn(CTX, char* exe, char** argv)
{
	int pid, status;
	int rd, fd = ctx->sigfd;
	struct sigevent se;

	if((pid = sys_fork()) < 0)
		return warn("fork", NULL, pid);
	if(pid == 0)
		_exit(child(exe, argv, ctx->envp));

	while((rd = sys_read(fd, &se, sizeof(se))) > 0) {
		if(rd < sizeof(se))
			quit(ctx, "bad sigevent size", NULL, rd);

		if(se.signo == SIGINT)
			sys_kill(pid, SIGINT);
		else if(se.signo == SIGQUIT)
			sys_kill(pid, SIGQUIT);
		else if(se.signo != SIGCHLD)
			continue;
		else if(trywaitpid(pid, &status) >= 0)
			break;
	}

	if(WIFSIGNALED(status))
		warn("killed by signal", NULL, WTERMSIG(status));
}

static int try_cmd_at(CTX, char** argv, char* ds, char* de)
{
	char* cmd = *argv;
	int clen = strlen(cmd);
	long dlen = de - ds;

	FMTBUF(p, e, path, clen + dlen + 2);
	p = fmtraw(p, e, ds, dlen);
	p = fmtchar(p, e, '/');
	p = fmtraw(p, e, cmd, clen);
	FMTEND(p, e);

	if(sys_access(path, X_OK))
		return 0;

	spawn(ctx, path, argv);

	return 1;
}

static void pathwalk(CTX, char** argv)
{
	char* path;

	if(!(path = getenv(ctx->envp, "PATH")))
		return warn("no $PATH set", NULL, 0);

	char* p = path;
	char* e = path + strlen(path);

	while(p < e) {
		char* q = strecbrk(p, e, ':');

		if(try_cmd_at(ctx, argv, p, q))
			return;

		p = q + 1;
	}

	warn("command not found:", *argv, 0);
}

static int lookslikepath(const char* file)
{
	const char* p;

	for(p = file; *p; p++)
		if(*p == '/')
			return 1;

	return 0;
}

void execute(CTX, int argc, char** argv)
{
	const struct builtin* fn;

	if(lookslikepath(*argv))
		return spawn(ctx, *argv, argv);
	if((fn = find_builtin(*argv)))
		return runbi(ctx, fn, argc, argv);

	return pathwalk(ctx, argv);
}
