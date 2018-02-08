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

/* Command line args stuff */

static void init_args(CTX, int argc, char** argv)
{
	ctx->argi = 1;
	ctx->argc = argc;
	ctx->argv = argv;
}

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

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		return NULL;

	return ctx->argv[ctx->argi++];
}

void connect_wictl_start(CTX)
{
	if(connect_wictl_(ctx) >= 0)
		return;

	try_start_wienc(ctx, NULL);

	connect_wictl(ctx);
}

void connect_wictl_check(CTX)
{
	if(connect_wictl_(ctx) >= 0)
		return;

	fail("service is not running", NULL, 0);
}

/* User commands */

static void cmd_status(CTX)
{
	struct ucmsg* msg;

	uc_put_hdr(UC, CMD_WI_STATUS);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_wictl_check(ctx);

	msg = send_recv_msg(ctx);

	dump_status(ctx, msg);
}

static void cmd_bss(CTX)
{
	ctx->showbss = 1;
	cmd_status(ctx);
}

static void cmd_neutral(CTX)
{
	struct ucmsg* msg;
	int ret;

	uc_put_hdr(UC, CMD_WI_NEUTRAL);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_wictl(ctx);

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
}

static void cmd_scan(CTX)
{
	struct ucmsg* msg;

	uc_put_hdr(UC, CMD_WI_SCAN);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_wictl_start(ctx);

	send_check(ctx);

	while((msg = recv_reply(ctx))) {
		if(msg->cmd == REP_WI_SCAN_FAIL)
			fail("scan failed", NULL, 0);
		if(msg->cmd == REP_WI_SCAN_DONE)
			break;
		if(msg->cmd == REP_WI_NET_DOWN)
			fail("net down", NULL, 0);
	}

	uc_put_hdr(UC, CMD_WI_STATUS);
	uc_put_end(UC);

	msg = send_recv_msg(ctx);

	dump_scanlist(ctx, msg);
}

static void cmd_stop(CTX)
{
	struct ucmsg* msg;
	int* ifi;

	no_other_options(ctx);

	connect_wictl(ctx);

	uc_put_hdr(UC, CMD_WI_DEVICE);
	uc_put_end(UC);

	msg = send_recv_msg(ctx);

	if(!(ifi = uc_get_int(msg, ATTR_IFI)))
		fail("invalid reply from wienc", NULL, 0);

	connect_ifctl(ctx);

	uc_put_hdr(UC, CMD_IF_STOP);
	uc_put_int(UC, ATTR_IFI, *ifi);
	uc_put_end(UC);

	send_check(ctx);
}

static void cmd_start(CTX)
{
	char* dev = shift_arg(ctx);

	no_other_options(ctx);

	try_start_wienc(ctx, dev);
}

static void wait_for_connect(CTX)
{
	struct ucmsg* msg;
	int failures = 0;

	while((msg = recv_reply(ctx))) switch(msg->cmd) {
		case REP_WI_NET_DOWN:
			fail(NULL, NULL, -ENETDOWN);
		case REP_WI_CONNECTED:
			warn_sta(ctx, "connected to", msg);
			return;
		case REP_WI_DISCONNECT:
			warn_sta(ctx, "cannot connect to", msg);
			failures++;
			break;
		case REP_WI_NO_CONNECT:
			if(failures)
				fail("no more APs in range", NULL, 0);
			else
				fail("no suitable APs in range", NULL, 0);
	}
}

static void cmd_fixedap(CTX)
{
	char *ssid = shift_arg(ctx);

	if(!ssid) fail("need AP ssid", NULL, 0);

	int slen = strlen(ssid);
	int ret;

	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_bin(UC, ATTR_SSID, ssid, slen);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_wictl_start(ctx);

	if((ret = send_recv_cmd(ctx)) >= 0)
		goto got;
	if(ret != -ENOKEY || !ssid)
		fail(NULL, NULL, ret);

	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_bin(UC, ATTR_SSID, ssid, slen);
	put_psk_input(ctx, ssid, slen);
	uc_put_end(UC);

	send_check(ctx);
got:
	wait_for_connect(ctx);
}

static void cmd_connect(CTX)
{
	if(got_any_args(ctx))
		return cmd_fixedap(ctx);

	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_wictl_start(ctx);
	send_check(ctx);

	wait_for_connect(ctx);
}

static void cmd_forget(CTX)
{
	char *ssid;
	int slen;

	if(!(ssid = shift_arg(ctx)))
		fail("SSID required", NULL, 0);

	slen = strlen(ssid);

	uc_put_hdr(UC, CMD_WI_FORGET);
	uc_put_bin(UC, ATTR_SSID, ssid, slen);
	uc_put_end(UC);

	no_other_options(ctx);
	connect_wictl_start(ctx);

	send_check(ctx);
}

typedef void (*cmdptr)(CTX);

static const struct cmdrec {
	char name[12];
	cmdptr call;
} commands[] = {
	{ "scan",       cmd_scan    },
	{ "ap",         cmd_fixedap },
	{ "connect",    cmd_connect },
	{ "dc",         cmd_neutral },
	{ "break",      cmd_neutral },
	{ "disconnect", cmd_neutral },
	{ "stop",       cmd_stop    },
	{ "start",      cmd_start   },
	{ "forget",     cmd_forget  },
	{ "bss",        cmd_bss     }
};

static void dispatch(CTX, char* name)
{
	const struct cmdrec* r;

	for(r = commands; r < commands + ARRAY_SIZE(commands); r++)
		if(!strncmp(r->name, name, sizeof(r->name)))
			return r->call(ctx);

	fail("unknown command", name, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	char* cmd;

	memzero(ctx, sizeof(ctx));

	init_args(ctx, argc, argv);
	init_heap_bufs(ctx);

	if((cmd = shift_arg(ctx)))
		dispatch(ctx, cmd);
	else
		cmd_status(ctx);

	return 0;
}
