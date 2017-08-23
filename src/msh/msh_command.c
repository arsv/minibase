#include <sys/file.h>
#include <sys/proc.h>

#include <string.h>
#include <format.h>
#include <exit.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

static const struct cmd {
	char name[12];
	int (*func)(struct sh* ctx);
} builtins[] = {
#define CMD(name) \
	{ #name, cmd_##name },
#include "msh_cmd.h"
	{ "", NULL }
};

static int child(struct sh* ctx, char* cmd)
{
	long ret = execvpe(cmd, ctx->argv, ctx->envp);
	error(ctx, "exec", cmd, ret);
	return 0xFF;
}

static int describe(struct sh* ctx, int status)
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

	FMTEND(p);

	return error(ctx, msg, buf, 0);
}

static int spawn(struct sh* ctx, int dash)
{
	long pid = sys_fork();
	char* cmd = *ctx->argv;
	int status;

	if(pid < 0)
		fail(ctx, "fork", NULL, pid);
	if(pid == 0)
		_exit(child(ctx, cmd));

	if((pid = sys_waitpid(pid, &status, 0)) < 0)
		fail(ctx, "wait", cmd, pid);

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

void command(struct sh* ctx)
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

	if(ret && !ctx->dash)
		_exit(0xFF);
}
