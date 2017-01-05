#include <sys/fork.h>
#include <sys/_exit.h>
#include <sys/chdir.h>
#include <sys/waitpid.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"

#define NLEN 11

static int fchk(long ret, struct sh* ctx, const char* msg, char* arg)
{
	if(ret < 0)
		return error(ctx, msg, arg, ret);
	else
		return 0;
}

static int cmd_cd(struct sh* ctx, int argc, char** argv)
{
	return fchk(syschdir(argv[1]),
			ctx, "chdir", argv[1]);
}

static int cmd_exit(struct sh* ctx, int argc, char** argv)
{
	_exit(0);
}

static int cmd_exec(struct sh* ctx, int argc, char** argv)
{
	if(argc < 2)
		return error(ctx, "exec: too few arguments", NULL, 0);

	long ret = execvpe(argv[1], argv+1, ctx->envp);

	return error(ctx, "exec", argv[1], ret);
}

static const struct cmd {
	char name[NLEN];
	int (*cmd)(struct sh* ctx, int argc, char** argv);
} builtins[] = {
	{ "cd",   cmd_cd   },
	{ "exit", cmd_exit },
	{ "exec", cmd_exec },
	{ "",   NULL }
};

static void builtin(struct sh* ctx, const struct cmd* bi, int argc, char** argv)
{
	ctx->ret = bi->cmd(ctx, argc, argv);
}

static void spawn(struct sh* ctx, int argc, char** argv)
{
	long pid = sysfork();

	if(pid < 0)
		fail("fork", NULL, pid);

	if(!pid) {
		long ret = execvpe(*argv, argv, ctx->envp);
		error(ctx, "exec", *argv, ret);
		_exit(0xFF);
	}

	if((pid = syswaitpid(pid, &ctx->ret, 0)) < 0)
		fail("wait", *argv, pid);
}

void exec(struct sh* ctx, int argc, char** argv)
{
	const struct cmd* bi;
	int noerror = 0;

	if(argv[0][0] == '-') {
		noerror = 1;
		argv[0]++;
	}

	for(bi = builtins; bi->cmd; bi++)
		if(!strcmp(bi->name, argv[0]))
			break;
	if(bi->cmd)
		builtin(ctx, bi, argc, argv);
	else
		spawn(ctx, argc, argv);

	if(!ctx->ret || noerror)
		return;

	fatal(ctx, "command failed", NULL);
}
