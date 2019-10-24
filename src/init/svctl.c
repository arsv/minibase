#include <sys/reboot.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "svctl.h"

ERRTAG("svctl");
ERRLIST(NENOENT NECONNREFUSED NELOOP NENFILE NEMFILE NEINTR NEINVAL NEACCES
	NEPERM NEIO NEFAULT NENOSYS NEALREADY NEINPROGRESS);

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
	if(argc > 1 && argv[1][0] == '-')
		fail("no top-level options allowed", NULL, 0);

	ctx->argi = 1;
	ctx->argc = argc;
	ctx->argv = argv;
}

static struct ucmsg* recv_ok_reply(CTX)
{
	struct ucmsg* msg = recv_reply(ctx);
	int cmd = msg->cmd;

	if(cmd < 0)
		fail(NULL, NULL, cmd);

	return msg;
}

static void recv_empty_ok(CTX)
{
	(void)recv_ok_reply(ctx);
}

static void recv_ok_or_already(CTX)
{
	struct ucmsg* msg = recv_reply(ctx);
	int cmd = msg->cmd;

	if(cmd < 0 && cmd != -EALREADY)
		fail(NULL, NULL, msg->cmd);
}

static char* single_arg(CTX)
{
	char* arg;

	if(!(arg = shift_arg(ctx)))
		fail("too few arguments", NULL, 0);

	no_other_options(ctx);

	return arg;
}

static void send_proc_cmd(CTX, int cmd, char* name)
{
	start_request(ctx, cmd, 1, strlen(name));
	add_str_attr(ctx, ATTR_NAME, name);
	send_request(ctx);
}

static void simple_proc_cmd(CTX, int cmd)
{
	char* name = single_arg(ctx);

	send_proc_cmd(ctx, cmd, name);
	recv_empty_ok(ctx);
}

static void simple_global(CTX, int cmd)
{
	no_other_options(ctx);

	start_request(ctx, cmd, 0, 0);
	send_request(ctx);

	recv_empty_ok(ctx);
}

static void cmd_start(CTX)
{
	simple_proc_cmd(ctx, CMD_START);
}

static void cmd_stop(CTX)
{
	simple_proc_cmd(ctx, CMD_STOP);
}

static void cmd_restart(CTX)
{
	char* name = single_arg(ctx);

	send_proc_cmd(ctx, CMD_STOP, name);
	recv_ok_or_already(ctx);

	send_proc_cmd(ctx, CMD_START, name);
	recv_empty_ok(ctx);
}

static void cmd_hup(CTX)
{
	simple_proc_cmd(ctx, CMD_HUP);
}

static void cmd_pause(CTX)
{
	simple_proc_cmd(ctx, CMD_PAUSE);
}

static void cmd_resume(CTX)
{
	simple_proc_cmd(ctx, CMD_RESUME);
}

static void cmd_flush(CTX)
{
	simple_proc_cmd(ctx, CMD_FLUSH);
}

static void cmd_pidof(CTX)
{
	char* name = single_arg(ctx);

	send_proc_cmd(ctx, CMD_GETPID, name);
	dump_pid(ctx, recv_ok_reply(ctx));
}

static void cmd_show(CTX)
{
	char* name = single_arg(ctx);

	send_proc_cmd(ctx, CMD_STATUS, name);
	expect_large(ctx);
	dump_info(ctx, recv_ok_reply(ctx));
}

static void cmd_list(CTX)
{
	no_other_options(ctx);

	start_request(ctx, CMD_LIST, 0, 0);
	send_request(ctx);

	expect_large(ctx);
	dump_list(ctx, recv_ok_reply(ctx));
}

static void cmd_reload(CTX)
{
	simple_global(ctx, CMD_RELOAD);
}

static void cmd_reboot(CTX)
{
	simple_global(ctx, CMD_REBOOT);
}

static void cmd_shutdown(CTX)
{
	simple_global(ctx, CMD_SHUTDOWN);
}

static void cmd_poweroff(CTX)
{
	simple_global(ctx, CMD_POWEROFF);
}

static void cmd_flushall(CTX)
{
	simple_global(ctx, CMD_FLUSH);
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
	{ "flush-all", cmd_flushall },
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
	cmdptr handler;

	init_args(ctx, argc, argv);

	if(argc > 1)
		handler = resolve(shift_arg(ctx));
	else
		handler = cmd_list;

	init_socket(ctx);

	handler(ctx);

	flush_output(ctx);

	return 0;
}
