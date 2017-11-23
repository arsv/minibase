#include <sys/reboot.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "common.h"
#include "svctl.h"

ERRTAG("svc");
ERRLIST(NENOENT NECONNREFUSED NELOOP NENFILE NEMFILE NEINTR NEINVAL NEACCES
	NEPERM NEIO NEFAULT NENOSYS);

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

static int count_args(CTX)
{
	return (ctx->argc - ctx->argi);
}

static int sum_length(CTX)
{
	int i, len = 0;

	for(i = ctx->argi; i < ctx->argc; i++)
		len += strlen(ctx->argv[i]);

	return len;
}

static void init_args(CTX, int argc, char** argv)
{
	if(argc > 1 && argv[1][0] == '-')
		fail("no top-level options allowed", NULL, 0);

	ctx->argi = 1;
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

static void multi_name_req(CTX, int cmd, int argreq)
{
	int count = count_args(ctx);
	int length = sum_length(ctx);
	char* name;

	if(!count && argreq)
		fail("too few arguments", NULL, 0);

	start_request(ctx, cmd, count, length);

	while((name = shift_arg(ctx)))
		add_str_attr(ctx, ATTR_NAME, name);

	send_request(ctx);
	recv_empty(ctx);
}

static void cmd_start(CTX)
{
	multi_name_req(ctx, CMD_ENABLE, 1);
}

static void cmd_stop(CTX)
{
	multi_name_req(ctx, CMD_DISABLE, 1);
}

static void cmd_restart(CTX)
{
	multi_name_req(ctx, CMD_RESTART, 1);
}

static void cmd_hup(CTX)
{
	multi_name_req(ctx, CMD_HUP, 1);
}

static void cmd_pause(CTX)
{
	multi_name_req(ctx, CMD_PAUSE, 1);
}

static void cmd_resume(CTX)
{
	multi_name_req(ctx, CMD_RESUME, 1);
}

static void cmd_flush(CTX)
{
	multi_name_req(ctx, CMD_FLUSH, 0);
}

static void cmd_pidof(CTX)
{
	char* name;

	if(!(name = shift_arg(ctx)))
		fail("too few arguments", NULL, 0);

	no_other_options(ctx);

	start_request(ctx, CMD_GETPID, 1, strlen(name));
	add_str_attr(ctx, ATTR_NAME, name);

	send_request(ctx);
	recv_dump(ctx, name, dump_pid);
}

static void cmd_list(CTX)
{
	no_other_options(ctx);

	start_request(ctx, CMD_LIST, 0, 0);
	send_request(ctx);

	expect_large(ctx);
	recv_dump(ctx, NULL, dump_list);
}

static void cmd_show(CTX)
{
	char* name;

	if(!(name = shift_arg(ctx)))
		fail("service name required", NULL, 0);

	no_other_options(ctx);

	start_request(ctx, CMD_STATUS, 1, strlen(name));
	add_str_attr(ctx, ATTR_NAME, name);
	send_request(ctx);

	expect_large(ctx);
	recv_dump(ctx, name, dump_info);
}

static void cmd_reload(CTX)
{
	no_other_options(ctx);

	start_request(ctx, CMD_RELOAD, 0, 0);
	send_request(ctx);

	recv_empty(ctx);
}

static void shutdown(CTX, int cmd)
{
	no_other_options(ctx);

	start_request(ctx, cmd, 0, 0);
	send_request(ctx);
	recv_empty(ctx);
}

static void cmd_reboot(CTX)
{
	shutdown(ctx, CMD_REBOOT);
}

static void cmd_shutdown(CTX)
{
	shutdown(ctx, CMD_SHUTDOWN);
}

static void cmd_poweroff(CTX)
{
	shutdown(ctx, CMD_POWEROFF);
}

static const struct cmdrec {
	char name[12];
	void (*cmd)(CTX);
} commands[] = {
	{ "hup",       cmd_hup      },
	{ "pidof",     cmd_pidof    },
	{ "pause",     cmd_pause    },
	{ "restart",   cmd_restart  },
	{ "start",     cmd_start    },
	{ "stop",      cmd_stop     },
	{ "flush",     cmd_flush    },
	{ "resume",    cmd_resume   },
	{ "reload",    cmd_reload   },
	{ "reboot",    cmd_reboot   },
	{ "shutdown",  cmd_shutdown },
	{ "poweroff",  cmd_poweroff },
	{ "show",      cmd_show     }
};

typedef void (*cmdptr)(CTX);

static cmdptr resolve(char* name)
{
	const struct cmdrec* p;

	for(p = commands; p < commands + ARRAY_SIZE(commands); p++)
		if(!strncmp(p->name, name, sizeof(p->name)))
			return p->cmd;

	fail("unknown command", name, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	memzero(&context, sizeof(context));
	cmdptr cmd;

	init_args(ctx, argc, argv);

	if(argc > 1)
		cmd = resolve(shift_arg(ctx));
	else
		cmd = cmd_list;

	init_socket(ctx);

	cmd(ctx);

	flush_output(ctx);

	return 0;
}
