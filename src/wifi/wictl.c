#include <bits/errno.h>

#include <nlusctl.h>
#include <format.h>
#include <heap.h>
#include <util.h>
#include <fail.h>

#include "config.h"
#include "common.h"
#include "wictl.h"

ERRTAG = "wictl";
ERRLIST = {
	REPORT(ENOENT), REPORT(EINVAL), REPORT(ENOSYS), REPORT(ENOENT),
	REPORT(EACCES), REPORT(EPERM), RESTASNUMBERS
};

#define OPTS "drsepw"
#define OPT_d (1<<0)
#define OPT_r (1<<1)
#define OPT_s (1<<2)
#define OPT_e (1<<3)
#define OPT_p (1<<4)
#define OPT_w (1<<5)

static void no_other_options(int opts, int i, int argc)
{
	if(i < argc)
		fail("too many arguments", NULL, 0);
	if(opts)
		fail("bad options", NULL, 0);
}

static void cmd_status(struct top* ctx, int opts, int i, int argc, char** argv)
{
	no_other_options(opts, i, argc);
	uc_put_hdr(&ctx->tx, CMD_STATUS);
	dump_status(ctx, send_check(ctx));
}

static void cmd_wired(struct top* ctx, int opts, int i, int argc, char** argv)
{
	no_other_options(opts, i, argc);
	uc_put_hdr(&ctx->tx, CMD_WIRED);
	send_check_empty(ctx);
}

static void cmd_scan(struct top* ctx, int opts, int i, int argc, char** argv)
{
	no_other_options(opts, i, argc);
	uc_put_hdr(&ctx->tx, CMD_SCAN);
	dump_scanlist(ctx, send_check(ctx));
}

static void cmd_roaming(struct top* ctx, int opts, int i, int argc, char** argv)
{
	no_other_options(opts, i, argc);
	uc_put_hdr(&ctx->tx, CMD_ROAMING);
	send_check_empty(ctx);
}

static void cmd_fixedap(struct top* ctx, int opts, int i, int argc, char** argv)
{
	fail("not implemented:", __FUNCTION__, 0);
}

static void cmd_neutral(struct top* ctx, int opts, int i, int argc, char** argv)
{
	no_other_options(opts, i, argc);
	uc_put_hdr(&ctx->tx, CMD_NEUTRAL);
	send_check_empty(ctx);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	struct top ctx;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	int mode = opts & (OPT_r | OPT_d | OPT_s | OPT_e | OPT_w);

	opts &= ~mode;

	top_init(&ctx);

	if(!mode && i >= argc)
		cmd_status(&ctx, opts, i, argc, argv);
	else if(mode == OPT_d)
		cmd_neutral(&ctx, opts, i, argc, argv);
	else if(mode == OPT_s)
		cmd_scan(&ctx, opts, i, argc, argv);
	else if(mode == OPT_e)
		cmd_wired(&ctx, opts, i, argc, argv);
	else if(mode == OPT_w && i >= argc)
		cmd_roaming(&ctx, opts, i, argc, argv);
	else if(mode == OPT_w || !mode)
		cmd_fixedap(&ctx, opts, i, argc, argv);
	else
		fail("bad options", NULL, 0);

	return 0;
}
