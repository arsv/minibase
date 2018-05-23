#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "vtctl.h"

ERRTAG("vtctl");
ERRLIST(NENOENT NECONNREFUSED NELOOP NENFILE NEMFILE NEINTR
	NEINVAL NEACCES NEPERM NEIO NEFAULT NENOSYS);

#define OPTS "sbku"
#define OPT_s (1<<0)
#define OPT_b (1<<1)
#define OPT_k (1<<2)
#define OPT_u (1<<3)

static void no_other_options(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("bad options", NULL, 0);
}

static char* shift_arg(CTX)
{
	if(ctx->argi < ctx->argc)
		return ctx->argv[ctx->argi++];
	else
		return NULL;
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

	start_request(ctx, CMD_STATUS);
	send_request(ctx);

	recv_dump(ctx, NULL, dump_status);
}

static void cmd_switch(CTX, char* ttystr)
{
	int tty;
	char* p;

	if(!(p = parseint(ttystr, &tty)) || *p)
		fail("bad tty id", ttystr, 0);

	no_other_options(ctx);

	start_request(ctx, CMD_SWITCH);
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

	start_request(ctx, CMD_SPAWN);
	add_str_attr(ctx, ATTR_NAME, name);
	send_request(ctx);

	recv_empty(ctx);
}

static void simple(CTX, int cmd)
{
	no_other_options(ctx);

	start_request(ctx, cmd);
	send_request(ctx);

	recv_empty(ctx);
}

static void cmd_swback(CTX)
{
	simple(ctx, CMD_SWBACK);
}

static void cmd_swlock(CTX)
{
	simple(ctx, CMD_SWLOCK);
}

static void cmd_unlock(CTX)
{
	simple(ctx, CMD_UNLOCK);
}

static const struct cmdrec {
	char name[8];
	void (*cmd)(CTX);
} commands[] = {
	{ "spawn",  cmd_spawn  },
	{ "back",   cmd_swback },
	{ "lock",   cmd_swlock },
	{ "unlock", cmd_unlock }
};

static void dispatch(CTX, char* name)
{
	const struct cmdrec* cr;

	if(*name >= '0' && *name <= '9')
		return cmd_switch(ctx, name);

	for(cr = commands; cr < commands + ARRAY_SIZE(commands); cr++)
		if(!strncmp(cr->name, name, sizeof(cr->name)))
			return cr->cmd(ctx);

	fail("unknown command", name, 0);
}

int main(int argc, char** argv)
{
	char* name;

	struct top context, *ctx = &context;
	memzero(&context, sizeof(context));

	init_args(ctx, argc, argv);
	init_socket(ctx);

	if(!(name = shift_arg(ctx)))
		cmd_status(ctx);
	else
		dispatch(ctx, name);

	return 0;
}
