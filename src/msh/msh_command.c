#include <sys/file.h>
#include <sys/proc.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct cmd {
	char name[12];
	int (*func)(CTX);
} builtins[] = {
#define CMD(name) \
	{ #name, cmd_##name },
#include "msh_cmd.h"
	{ "", NULL }
};

static int child(CTX, char* cmd)
{
	long ret = execvpe(cmd, ctx->argv, ctx->envp);
	error(ctx, "exec", cmd, ret);
	return 0xFF;
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

static int spawn(CTX, int dash)
{
	long pid = sys_fork();
	char* cmd = *ctx->argv;
	int status;

	if(pid < 0)
		quit(ctx, "fork", NULL, pid);
	if(pid == 0)
		_exit(child(ctx, cmd));

	if((pid = sys_waitpid(pid, &status, 0)) < 0)
		quit(ctx, "wait", cmd, pid);

	if(!status || dash)
		return 0;

	return describe(ctx, status);
}

static const struct cmd* builtin(const char* name)
{
	const struct cmd* cc;
	int maxlen = sizeof(cc->name);

	for(cc = builtins; cc->func; cc++)
		if(!strncmp(cc->name, name, maxlen))
			return cc;

	return NULL;
}

static void exec_into_trap(CTX)
{
	char* argv[] = { ctx->trap, NULL };
	int ret = sys_execve(*argv, argv, ctx->envp);
	warn("exec", *argv, ret);
}

void command(CTX)
{
	const struct cmd* cc;
	int ret, dash;

	if(!ctx->argc)
		return;

	if((dash = (ctx->argv[0][0] == '-')))
		ctx->argv[0]++;

	if((cc = builtin(ctx->argv[0])))
		ret = cc->func(ctx);
	else
		ret = spawn(ctx, dash);

	if(!ret || ctx->dash)
		return;

	if(*ctx->trap)
		exec_into_trap(ctx);

	_exit(0xFF);
}
