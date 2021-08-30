#include <string.h>
#include <main.h>
#include <util.h>

#include "mpkg.h"

ERRTAG("mpkg");

char* shift(CTX)
{
	int i = ctx->argi;

	if(i >= ctx->argc)
		fail("too few arguments", NULL, 0);

	ctx->argi = i + 1;

	return ctx->argv[i];
}

int args_left(CTX)
{
	return (ctx->argc - ctx->argi);
}

void no_more_arguments(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
}

static void dispatch_command(CTX, char* cmd)
{
	if(!strcmp(cmd, "deploy"))
		return cmd_deploy(ctx);
	if(!strcmp(cmd, "remove"))
		return cmd_remove(ctx);
	if(!strcmp(cmd, "list"))
		return cmd_list(ctx);

	fail("unknown command", cmd, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->argi = 1;
	ctx->envp = argv + argc + 1;

	ctx->at = -1;
	ctx->nullfd = -1;
	ctx->pacfd = -1;
	ctx->lstfd = -1;

	if(argc < 2)
		fail("too few arguments", NULL, 0);

	char* cmd = shift(ctx);

	dispatch_command(ctx, cmd);

	return 0;
}
