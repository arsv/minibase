#include <sys/proc.h>

#include <string.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static int child(CTX, char** argv)
{
	int ret = execvpe(*argv, argv, ctx->envp);
	error(ctx, "exec", *argv, ret);
	return 0xFF;
}

int cmd_run(CTX)
{
	int pid, status;

	if(noneleft(ctx))
		fatal(ctx, "missing command", NULL);

	char** argv = argsleft(ctx);

	if((pid = sys_fork()) < 0)
		return error(ctx, "fork", NULL, pid);

	if(pid == 0)
		_exit(child(ctx, argv));

	if((pid = sys_waitpid(pid, &status, 0)) < 0)
		return error(ctx, "waitpid", NULL, pid);

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
