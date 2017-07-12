#include <bits/errno.h>
#include <sys/reboot.h>
#include <string.h>
#include <format.h>
#include <fail.h>
#include <util.h>

#include "config.h"
#include "common.h"
#include "svc.h"

ERRTAG = "svc";
ERRLIST = {
	REPORT(ENOENT), REPORT(ECONNREFUSED), REPORT(ELOOP), REPORT(ENFILE),
	REPORT(EMFILE), REPORT(EINTR), REPORT(EINVAL), REPORT(EACCES),
	REPORT(EPERM), REPORT(EIO), REPORT(EFAULT), REPORT(ENOSYS),
	RESTASNUMBERS
};

#define OPTS "hiprstwqxz"
#define OPT_h (1<<0)
#define OPT_i (1<<1)
#define OPT_p (1<<2)
#define OPT_r (1<<3)
#define OPT_s (1<<4)
#define OPT_t (1<<5)
#define OPT_w (1<<6)
#define OPT_q (1<<7)
#define OPT_x (1<<8)
#define OPT_z (1<<9)

#define UCBUF(n, l) \
	char txbuf[n*sizeof(struct ucattr) + 4*n + l + 10]; \
	ctx->uc = (struct ucbuf) { \
		.brk = txbuf, \
		.ptr = txbuf, \
		.end = txbuf + sizeof(txbuf), \
		.over = 0 \
	}

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
	recv_dump(ctx, NULL, dump_msg);
}

static void multi_name_req(CTX, int cmd)
{
	int count = count_args(ctx);
	int length = sum_length(ctx);
	char* name;

	if(!count)
		fail("too few arguments", NULL, 0);

	UCBUF(count, length);
	uc_put_hdr(UC, cmd);

	while((name = shift_arg(ctx)))
		uc_put_str(UC, ATTR_NAME, name);

	uc_put_end(UC);

	send_command(ctx);

	init_recv_heap(ctx);
	recv_empty(ctx);
}

static void cmd_start(CTX)
{
	multi_name_req(ctx, CMD_START);
}

static void cmd_stop(CTX)
{
	multi_name_req(ctx, CMD_STOP);
}

static void cmd_restart(CTX)
{
	multi_name_req(ctx, CMD_RESTART);
}

static void cmd_hup(CTX)
{
	multi_name_req(ctx, CMD_HUP);
}

static void cmd_pause(CTX)
{
	multi_name_req(ctx, CMD_PAUSE);
}

static void cmd_resume(CTX)
{
	multi_name_req(ctx, CMD_RESUME);
}

static void cmd_pidof(CTX)
{
	char* name;

	init_recv_small(ctx);

	if(!(name = shift_arg(ctx)))
		fail("too few arguments", NULL, 0);

	UCBUF(1, strlen(name));
	uc_put_hdr(UC, CMD_PIDOF);
	uc_put_str(UC, ATTR_NAME, name);
	uc_put_end(UC);

	no_other_options(ctx);
	send_command(ctx);

	recv_dump(ctx, name, dump_pid);
}

static void cmd_status(CTX)
{
	char* name;
	UCBUF(1, 100);

	if((name = shift_arg(ctx))) {
		uc_put_hdr(UC, CMD_STATUS);
		uc_put_str(UC, ATTR_NAME, name);
		uc_put_end(UC);
	} else {
		uc_put_hdr(UC, CMD_LIST);
		uc_put_end(UC);
	}

	no_other_options(ctx);
	send_command(ctx);

	init_recv_heap(ctx);
	recv_dump(ctx, name, name ? dump_info : dump_list);
}

static void cmd_reload(CTX)
{
	UCBUF(0, 30);

	uc_put_hdr(UC, CMD_RELOAD);
	uc_put_end(UC);

	no_other_options(ctx);
	send_command(ctx);

	init_recv_small(ctx);
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
	UCBUF(0, 30);

	if(!(mode = shift_arg(ctx)))
		fail("argument required", NULL, 0);

	for(rc = rbcodes; rc->cmd; rc++)
		if(!strcmp(rc->name, mode))
			break;
	if(!rc->cmd)
		fail("unknown mode", mode, 0);

	uc_put_hdr(UC, rc->cmd);
	uc_put_end(UC);

	no_other_options(ctx);
	send_command(ctx);

	init_recv_small(ctx);
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

	return 0;
}
