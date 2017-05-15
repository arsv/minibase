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

#define OPTS "ewsipznq"
#define OPT_e (1<<0)
#define OPT_w (1<<1)
#define OPT_s (1<<2)
#define OPT_p (1<<4)
#define OPT_z (1<<5)
#define OPT_n (1<<6)
#define OPT_q (1<<7)

static void cmd_status(struct top* ctx, int opts, int i, int argc, char** argv)
{
	uc_put_hdr(&ctx->tx, CMD_STATUS);

	if(i < argc)
		fail("too many arguments", NULL, 0);
	if(opts)
		fail("bad options", NULL, 0);

	dump_status(ctx, send_check(ctx));
}

static void cmd_wless(struct top* ctx, int opts, int i, int argc, char** argv)
{
	char* ssid = NULL;
	uc_put_hdr(&ctx->tx, CMD_WLESS);

	if(i < argc)
		uc_put_str(&ctx->tx, ATTR_SSID, ssid = argv[i++]);
	if(i < argc && (opts & OPT_p))
		put_psk_arg(ctx, ssid, argv[i++]);
	else if(opts & OPT_p)
		put_psk_input(ctx, ssid);
	if(i < argc)
		fail("too many arguments", NULL, 0);
	if(opts & ~(OPT_p))
		fail("bad options", NULL, 0);

	send_check_empty(ctx);
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

static void cmd_stop(struct top* ctx, int opts, int i, int argc, char** argv)
{
	uc_put_hdr(&ctx->tx, CMD_STOP);

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
	send_check_empty(ctx);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	struct top ctx;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	int mode = opts & (OPT_e | OPT_w | OPT_s | OPT_z | OPT_q);

	opts &= ~mode;

	top_init(&ctx);

	if(!mode && i >= argc)
		cmd_status(&ctx, opts, i, argc, argv);
	else if(mode == OPT_w)
		cmd_wless(&ctx, opts, i, argc, argv);
	else if(mode == OPT_e)
		cmd_wired(&ctx, opts, i, argc, argv);
	else if(mode == OPT_s)
		cmd_stop(&ctx, opts, i, argc, argv);
	else if(mode == OPT_q)
		cmd_scan(&ctx, opts, i, argc, argv);
	else if(i < argc)
		cmd_wless(&ctx, opts, i, argc, argv);
	else
		fail("bad options", NULL, 0);

	return 0;
}
