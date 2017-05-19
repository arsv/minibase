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

#define OPTS "drsep"
#define OPT_d (1<<0)
#define OPT_r (1<<1)
#define OPT_s (1<<2)
#define OPT_e (1<<4)
#define OPT_p (1<<5)

static void cmd_status(struct top* ctx, int opts, int i, int argc, char** argv)
{
	uc_put_hdr(&ctx->tx, CMD_STATUS);

	if(i < argc)
		fail("too many arguments", NULL, 0);
	if(opts)
		fail("bad options", NULL, 0);

	dump_status(ctx, send_check(ctx));
}

static void cmd_wired(struct top* ctx, int opts, int i, int argc, char** argv)
{
	uc_put_hdr(&ctx->tx, CMD_WIRED);

	if(i < argc)
		fail("too many arguments", NULL, 0);
	if(opts)
		fail("bad options", NULL, 0);

	send_check_empty(ctx);
}

static void cmd_scan(struct top* ctx, int opts, int i, int argc, char** argv)
{
	if(i < argc)
		fail("too many arguments", NULL, 0);
	if(opts)
		fail("bad options", NULL, 0);

	uc_put_hdr(&ctx->tx, CMD_SCAN);

	dump_scanlist(ctx, send_check(ctx));
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	struct top ctx;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	int mode = opts & (OPT_r | OPT_r | OPT_s | OPT_e);

	opts &= ~mode;

	top_init(&ctx);

	if(!mode && i >= argc)
		cmd_status(&ctx, opts, i, argc, argv);
	else if(mode == OPT_s)
		cmd_scan(&ctx, opts, i, argc, argv);
	else if(mode == OPT_e)
		cmd_wired(&ctx, opts, i, argc, argv);
	else
		fail("bad options", NULL, 0);

	return 0;
}
