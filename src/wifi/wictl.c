#include <bits/errno.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <heap.h>
#include <util.h>
#include <fail.h>

#include "config.h"
#include "common.h"
#include "wictl.h"

ERRTAG = "wictl";
ERRLIST = {
	REPORT(ENOENT), REPORT(EINVAL), REPORT(ENOSYS), REPORT(ENOENT),
	REPORT(EACCES), REPORT(EPERM), REPORT(EBUSY), REPORT(EALREADY),
	REPORT(ENETDOWN), RESTASNUMBERS
};

#define OPTS "drsepw"
#define OPT_d (1<<0)
#define OPT_r (1<<1)
#define OPT_s (1<<2)
#define OPT_e (1<<3)
#define OPT_p (1<<4)
#define OPT_w (1<<5)

static void no_other_options(struct top* ctx)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("bad options", NULL, 0);
}

static int got_any_args(struct top* ctx)
{
	return (ctx->argi < ctx->argc);
}

static int use_opt(struct top* ctx, int opt)
{
	int ret = ctx->opts & opt;
	ctx->opts &= ~opt;
	return ret;
}

static char* shift_opt(struct top* ctx)
{
	if(ctx->argi >= ctx->argc)
		return NULL;
	return ctx->argv[ctx->argi++];
}

static void maybe_put_ifi(struct top* ctx)
{
	char *ifname;
	int ifi;

	if(!(ifname = shift_opt(ctx)))
		return;
	if((ifi = getifindex(ctx->fd, ifname)) <= 0)
		fail("bad ifname", ifname, ifi);

	uc_put_int(UC, ATTR_IFI, ifi);
}

static void cmd_neutral(struct top* ctx)
{
	no_other_options(ctx);
	uc_put_hdr(UC, CMD_NEUTRAL);
	send_check_empty(ctx);
}

static void cmd_status(struct top* ctx)
{
	no_other_options(ctx);
	uc_put_hdr(UC, CMD_STATUS);
	dump_status(ctx, send_check(ctx));
}

static void cmd_wired(struct top* ctx)
{
	no_other_options(ctx);
	uc_put_hdr(UC, CMD_WIRED);
	send_check_empty(ctx);
}

static void cmd_scan(struct top* ctx)
{
	uc_put_hdr(UC, CMD_SCAN);
	maybe_put_ifi(ctx);
	no_other_options(ctx);
	dump_scanlist(ctx, send_check(ctx));
}

static void cmd_roaming(struct top* ctx)
{
	uc_put_hdr(UC, CMD_ROAMING);
	maybe_put_ifi(ctx);
	no_other_options(ctx);
	send_check_empty(ctx);
}

static void cmd_fixedap(struct top* ctx)
{
	char *ssid, *pass;

	uc_put_hdr(UC, CMD_FIXEDAP);

	if(!(ssid = shift_opt(ctx)))
		fail("ssid required", NULL, 0);

	uc_put_bin(UC, ATTR_SSID, ssid, strlen(ssid));

	if((pass = shift_opt(ctx)))
		put_psk_arg(ctx, ssid, pass);
	else if(use_opt(ctx, OPT_p))
		put_psk_input(ctx, ssid);

	no_other_options(ctx);
	send_check_empty(ctx);
}

static void init_args(struct top* ctx, int argc, char** argv)
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

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	init_args(ctx, argc, argv);
	init_heap_socket(ctx);

	if(use_opt(ctx, OPT_d))
		cmd_neutral(ctx);
	else if(use_opt(ctx, OPT_s))
		cmd_scan(ctx);
	else if(use_opt(ctx, OPT_w))
		cmd_roaming(ctx);
	else if(use_opt(ctx, OPT_e))
		cmd_wired(ctx);
	else if(got_any_args(ctx))
		cmd_fixedap(ctx);
	else
		cmd_status(ctx);

	return 0;
}
