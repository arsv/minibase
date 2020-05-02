#include <string.h>
#include <main.h>
#include <util.h>

#include "cpio.h"

ERRTAG("cpio");

char* shift(CTX)
{
	int i = ctx->argi;

	if(i >= ctx->argc)
		fail("too few arguments", NULL, 0);

	ctx->argi = i + 1;

	return ctx->argv[i];
}

void no_more_arguments(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
}

static void dispatch_char(CTX, char* cmd)
{
	char key = *cmd;

	if(key == 'c')
		return cmd_create(ctx);
	if(key == 'x')
		return cmd_extract(ctx);
	if(key == 't')
		return cmd_list(ctx);
	if(key == 'p')
		return cmd_pack(ctx);

	fail("unknown command", cmd, 0);
}

static void dispatch_word(CTX, char* cmd)
{
	if(!strcmp(cmd, "create"))
		return cmd_create(ctx);
	if(!strcmp(cmd, "extract"))
		return cmd_extract(ctx);
	if(!strcmp(cmd, "pack"))
		return cmd_pack(ctx);
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

	ctx->at = -1;
	ctx->null = -1;

	if(argc < 2)
		fail("too few arguments", NULL, 0);

	char* cmd = shift(ctx);

	if(cmd[0] && !cmd[1])
		dispatch_char(ctx, cmd);
	else
		dispatch_word(ctx, cmd);

	return 0;
}
