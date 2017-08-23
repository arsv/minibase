#include <bits/errno.h>
#include <sys/reboot.h>
#include <string.h>
#include <format.h>
#include <fail.h>
#include <util.h>

#include "common.h"
#include "svc.h"

ERRTAG = "svc";
ERRLIST = {
	REPORT(ENOENT), REPORT(ECONNREFUSED), REPORT(ELOOP), REPORT(ENFILE),
	REPORT(EMFILE), REPORT(EINTR), REPORT(EINVAL), REPORT(EACCES),
	REPORT(EPERM), REPORT(EIO), REPORT(EFAULT), REPORT(ENOSYS),
	RESTASNUMBERS
};

#define OPTS "fhiprstwqxz"
#define OPT_f (1<<0)
#define OPT_h (1<<1)
#define OPT_i (1<<2)
#define OPT_p (1<<3)
#define OPT_r (1<<4)
#define OPT_s (1<<5)
#define OPT_t (1<<6)
#define OPT_w (1<<7)
#define OPT_q (1<<8)
#define OPT_x (1<<9)
#define OPT_z (1<<10)

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

static void cmd_status(CTX)
{
	char* name;

	if((name = shift_arg(ctx))) {
		start_request(ctx, CMD_STATUS, 1, strlen(name));
		add_str_attr(ctx, ATTR_NAME, name);
	} else {
		start_request(ctx, CMD_LIST, 0, 0);
	}

	no_other_options(ctx);
	send_request(ctx);

	expect_large(ctx);
	recv_dump(ctx, name, name ? dump_info : dump_list);
}

static void cmd_reload(CTX)
{
	no_other_options(ctx);

	start_request(ctx, CMD_RELOAD, 0, 0);
	send_request(ctx);

	recv_empty(ctx);
}

static const struct rbcode {
	char name[12];
	int cmd;
} rbcodes[] = {
	{ "reboot",   CMD_REBOOT   },
	{ "shutdown", CMD_SHUTDOWN },
	{ "poweroff", CMD_POWEROFF },
	{ "",         0            }
};

static void cmd_shutdown(CTX)
{
	char* mode;
	const struct rbcode* rc;

	if(!(mode = shift_arg(ctx)))
		fail("argument required", NULL, 0);

	for(rc = rbcodes; rc->cmd; rc++)
		if(!strcmp(rc->name, mode))
			break;
	if(!rc->cmd)
		fail("unknown mode", mode, 0);

	no_other_options(ctx);

	start_request(ctx, rc->cmd, 0, 0);
	send_request(ctx);

	recv_empty(ctx);
}

static const struct cmdrec {
	int opt;
	void (*cmd)(CTX);
} commands[] = {
	{ OPT_h, cmd_hup      },
	{ OPT_i, cmd_pidof    },
	{ OPT_p, cmd_pause    },
	{ OPT_r, cmd_restart  },
	{ OPT_s, cmd_start    },
	{ OPT_t, cmd_stop     },
	{ OPT_f, cmd_flush    },
	{ OPT_w, cmd_resume   },
	{ OPT_q, cmd_reload   },
	{ OPT_x, cmd_shutdown },
	{     0, cmd_status   }
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

	cr->cmd(ctx);

	flush_output(ctx);

	return 0;
}
