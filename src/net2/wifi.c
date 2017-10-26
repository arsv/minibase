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

#define OPTS "asdp"
#define OPT_a (1<<0)
#define OPT_s (1<<1)
#define OPT_d (1<<2)
#define OPT_p (1<<3)

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

static void check_not_null(MSG)
{
	if(!msg) fail("connection lost", NULL, 0);
}

static void req_status(CTX)
{
	struct ucmsg* msg;

	uc_put_hdr(UC, CMD_WI_STATUS);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_socket(ctx, 0);

	msg = send_recv_msg(ctx);

	dump_status(ctx, msg);
}

static void req_neutral(CTX)
{
	struct ucmsg* msg;
	int ret;

	uc_put_hdr(UC, CMD_WI_NEUTRAL);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_socket(ctx, 0);

	if((ret = send_recv_cmd(ctx)) == -EALREADY)
		return;
	else if(ret < 0)
		fail(NULL, NULL, ret);

	while((msg = recv_reply(ctx))) {
		if(msg->cmd == REP_WI_DISCONNECT)
			break;
		if(msg->cmd == REP_WI_NET_DOWN)
			break;
	};

	check_not_null(msg);
}

static void req_scan(CTX)
{
	struct ucmsg* msg;

	uc_put_hdr(UC, CMD_WI_SCAN);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_socket(ctx, 1);

	send_check(ctx);

	while((msg = recv_reply(ctx))) {
		if(msg->cmd == REP_WI_SCAN_FAIL)
			fail("scan failed", NULL, 0);
		if(msg->cmd == REP_WI_SCAN_DONE)
			break;
		if(msg->cmd == REP_WI_NET_DOWN)
			fail("net down", NULL, 0);
	}

	check_not_null(msg);

	uc_put_hdr(UC, CMD_WI_STATUS);
	uc_put_end(UC);

	msg = send_recv_msg(ctx);

	dump_scanlist(ctx, msg);
}

static void wait_for_connect(CTX)
{
	struct ucmsg* msg;
	int failures = 0;

	while((msg = recv_reply(ctx))) switch(msg->cmd) {
		case REP_WI_SCANNING:
			warn("scanning", NULL, 0);
			break;
		case REP_WI_SCAN_FAIL:
			fail("scan failed", NULL, 0);
		case REP_WI_NET_DOWN:
			fail(NULL, NULL, -ENETDOWN);
		case REP_WI_CONNECTED:
			warn_sta("connected to", msg);
			return;
		case REP_WI_DISCONNECT:
			warn_sta("cannot connect to", msg);
			failures++;
			break;
		case REP_WI_NO_CONNECT:
			if(failures)
				fail("no more APs in range", NULL, 0);
			else
				fail("no suitable APs in range", NULL, 0);
	}

	check_not_null(msg);
}

static void req_connect(CTX)
{
	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_socket(ctx, 1);

	send_check(ctx);

	wait_for_connect(ctx);
}

static void req_fixedap(CTX)
{
	char *ssid;
	int slen;

	if(!(ssid = shift_arg(ctx)))
		fail("SSID required", NULL, 0);

	slen = strlen(ssid);

	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_bin(UC, ATTR_SSID, ssid, slen);

	if(use_opt(ctx, OPT_p))
		put_psk_input(ctx, ssid, slen);

	uc_put_end(UC);

	no_other_options(ctx);
	connect_socket(ctx, 1);

	send_check(ctx);

	wait_for_connect(ctx);
}

static void activate(CTX)
{
	if(got_any_args(ctx))
		req_fixedap(ctx);
	else
		req_connect(ctx);
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

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	init_args(ctx, argc, argv);
	init_heap_socket(ctx);

	if(use_opt(ctx, OPT_d))
		req_neutral(ctx);
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
