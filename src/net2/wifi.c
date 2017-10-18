#include <bits/errno.h>

#include <errtag.h>
#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <heap.h>
#include <util.h>

#include "common.h"
#include "wifi.h"

ERRTAG("wifi");
ERRLIST(NENOENT NEINVAL NENOSYS NENOENT NEACCES NEPERM NEBUSY NEALREADY
	NENETDOWN NENOKEY NENOTCONN NENODEV NETIMEDOUT);

#define OPTS "abcdepswxz"
#define OPT_a (1<<0)
#define OPT_b (1<<1)
#define OPT_c (1<<2)
#define OPT_d (1<<3)
#define OPT_e (1<<4)
#define OPT_p (1<<5)
#define OPT_s (1<<6)
#define OPT_w (1<<7)
#define OPT_x (1<<8)
#define OPT_z (1<<9)

static void no_other_options(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("bad options", NULL, 0);
}

static int got_any_args(CTX)
{
	return (ctx->argi < ctx->argc);
}

static int use_opt(CTX, int opt)
{
	int ret = ctx->opts & opt;
	ctx->opts &= ~opt;
	return ret;
}

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		return NULL;

	return ctx->argv[ctx->argi++];
}

static void req_neutral(CTX)
{
	uc_put_hdr(UC, CMD_WI_NEUTRAL);
	no_other_options(ctx);
	send_check_empty(ctx);
}

static void req_status(CTX)
{
	uc_put_hdr(UC, CMD_WI_STATUS);
	no_other_options(ctx);
	dump_status(ctx, send_check(ctx));
}

static void req_scan(CTX)
{
	uc_put_hdr(UC, CMD_WI_SCAN);
	no_other_options(ctx);
	dump_scanlist(ctx, send_check(ctx));
}

static void req_roaming(CTX)
{
	uc_put_hdr(UC, CMD_WI_ROAMING);
	no_other_options(ctx);
	send_check_empty(ctx);
}

static void req_fixedap(CTX)
{
	char *ssid;
	int slen;

	if(!(ssid = shift_arg(ctx)))
		fail("SSID required", NULL, 0);

	slen = strlen(ssid);

	uc_put_hdr(UC, CMD_WI_FIXEDAP);
	uc_put_bin(UC, ATTR_SSID, ssid, slen);

	if(use_opt(ctx, OPT_p))
		put_psk_input(ctx, ssid, slen);

	no_other_options(ctx);
	send_check_empty(ctx);
}

static void activate(CTX)
{
	if(got_any_args(ctx))
		req_fixedap(ctx);
	else
		req_roaming(ctx);
}

//static void cmd_setprio(CTX, int prio)
//{
//	char* ssid;
//
//	uc_put_hdr(UC, CMD_SETPRIO);
//
//	if(!(ssid = pop_arg(ctx)))
//		fail("ssid required", NULL, 0);
//
//	uc_put_bin(UC, ATTR_SSID, ssid, strlen(ssid));
//
//	if(prio < 10)
//		uc_put_int(UC, ATTR_PRIO, prio);
//
//	no_other_options(ctx);
//	send_check_empty(ctx);
//}

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

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	init_args(ctx, argc, argv);
	init_heap_socket(ctx);

	if(use_opt(ctx, OPT_d))
		req_neutral(ctx);
	//else if(use_opt(ctx, OPT_z))
	//	cmd_setprio(ctx, -1);
	//else if(use_opt(ctx, OPT_a))
	//	cmd_setprio(ctx, 2);
	//else if(use_opt(ctx, OPT_b))
	//	cmd_setprio(ctx, 1);
	//else if(use_opt(ctx, OPT_c))
	//	cmd_setprio(ctx, 0);
	else if(use_opt(ctx, OPT_s))
		req_scan(ctx);
	else if(use_opt(ctx, OPT_a))
		activate(ctx);
	else if(got_any_args(ctx))
		req_fixedap(ctx);
	else
		req_status(ctx);

	return 0;
}
