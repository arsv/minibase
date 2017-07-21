#include <bits/errno.h>
#include <string.h>
#include <format.h>
#include <fail.h>
#include <util.h>

#include "config.h"
#include "common.h"
#include "vtctl.h"

ERRTAG = "vtctl";
ERRLIST = {
	REPORT(ENOENT), REPORT(ECONNREFUSED), REPORT(ELOOP), REPORT(ENFILE),
	REPORT(EMFILE), REPORT(EINTR), REPORT(EINVAL), REPORT(EACCES),
	REPORT(EPERM), REPORT(EIO), REPORT(EFAULT), REPORT(ENOSYS),
	RESTASNUMBERS
};

#define OPTS "s"
#define OPT_s (1<<0)

static void no_other_options(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("bad options", NULL, 0);
}

static int use_opt(CTX, int opt)
{
	int ret = ctx->opts & opt;
	ctx->opts &= ~opt;
	return ret;
}

static char* shift_arg(CTX)
{
	if(ctx->argi < ctx->argc)
		return ctx->argv[ctx->argi++];
	else
		return NULL;
}

static char* shift_int(CTX, int* val)
{
	char *arg, *p;

	if(!(arg = shift_arg(ctx)))
		return arg;
	if(!(p = parseint(arg, val)) || *p)
		fail("integer argument required", NULL, 0);

	return arg;
}

static void init_args(CTX, int argc, char** argv)
{
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		ctx->opts = argbits(OPTS, argv[i++] + 1);
	else
		ctx->opts = 0;

	ctx->argi = i;
	ctx->argc = argc;
	ctx->argv = argv;
}

static void recv_dump(CTX, char* name, void (*dump)(CTX, MSG))
{
	struct ucmsg* msg;

	while((msg = recv_reply(ctx)))
		if(msg->cmd < 0)
			fail(NULL, name, msg->cmd);
		else if(msg->cmd > 0)
			continue;
		else break;

	if(dump) dump(ctx, msg);
}

static void recv_empty(CTX)
{
	recv_dump(ctx, NULL, NULL);
}

static void cmd_status(CTX)
{
	no_other_options(ctx);
	fail("not implemented", NULL, 0);
}

static void cmd_switch(CTX)
{
	int tty;

	if(!shift_int(ctx, &tty))
		fail("argument required", NULL, 0);

	no_other_options(ctx);

	start_request(ctx, CMD_SWITCH, 0, 0);
	add_int_attr(ctx, ATTR_TTY, tty);
	send_request(ctx);

	recv_empty(ctx);
}

static void cmd_spawn(CTX)
{
	char* name;

	if(!(name = shift_arg(ctx)))
		fail("argument required", NULL, 0);

	no_other_options(ctx);

	start_request(ctx, CMD_SWITCH, 0, 0);
	add_str_attr(ctx, ATTR_NAME, name);
	send_request(ctx);

	recv_empty(ctx);
}

static const struct cmdrec {
	int opt;
	void (*cmd)(CTX);
} commands[] = {
	{ OPT_s, cmd_spawn    },
	{     0, NULL         }
};

int main(int argc, char** argv)
{
	const struct cmdrec* cr;

	struct top context, *ctx = &context;
	memzero(&context, sizeof(context));

	init_args(ctx, argc, argv);
	init_socket(ctx);

	for(cr = commands; cr->opt; cr++)
		if(use_opt(ctx, cr->opt))
			break;

	if(cr->opt)
		cr->cmd(ctx);
	else if(argc > 1)
		cmd_switch(ctx);
	else
		cmd_status(ctx);

	return 0;
}
