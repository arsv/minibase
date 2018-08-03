#include <bits/errno.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <heap.h>
#include <util.h>
#include <main.h>

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

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		return NULL;

	return ctx->argv[ctx->argi++];
}

static void send_check_cmd(CTX)
{
	int ret;

	if((ret = send_recv_cmd(ctx)) < 0)
		fail(NULL, NULL, ret);
}

/* User commands */

static void cmd_device(CTX)
{
	char *name;
	
	if(!(name = shift_arg(ctx)))
		fail("need device name", NULL, 0);

	no_other_options(ctx);

	uc_put_hdr(UC, CMD_WI_SETDEV);
	uc_put_str(UC, ATTR_NAME, name);
	uc_put_end(UC);

	send_check_cmd(ctx);
}

static void cmd_status(CTX)
{
	struct ucmsg* msg;

	uc_put_hdr(UC, CMD_WI_STATUS);
	uc_put_end(UC);

	no_other_options(ctx);

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

static int request_scan(CTX)
{
	uc_put_hdr(UC, CMD_WI_SCAN);
	uc_put_end(UC);

	return send_recv_cmd(ctx);
}

static void wait_for_scan_results(CTX)
{
	struct ucmsg* msg;

	while((msg = recv_reply(ctx))) {
		if(msg->cmd == REP_WI_SCAN_FAIL)
			fail("scan failed", NULL, 0);
		if(msg->cmd == REP_WI_SCAN_DONE)
			break;
		if(msg->cmd == REP_WI_NET_DOWN)
			fail("net down", NULL, 0);
	}
}

static void fetch_dump_scan_list(CTX)
{
	struct ucmsg* msg;

	uc_put_hdr(UC, CMD_WI_STATUS);
	uc_put_end(UC);

	msg = send_recv_msg(ctx);

	if(msg->cmd < 0)
		fail(NULL, NULL, msg->cmd);

	dump_scanlist(ctx, msg);
}

static void set_scan_device(CTX, char* name)
{
	int ret;

	uc_put_hdr(UC, CMD_WI_SCANDEV);
	uc_put_str(UC, ATTR_NAME, name);
	uc_put_end(UC);

	if((ret = send_recv_cmd(ctx)) < 0)
		fail("cannot set device", name, ret);
}

static void pick_scan_device(CTX)
{
	char name[32];

	find_wifi_device(name);
	set_scan_device(ctx, name);
}

static void cmd_scan(CTX)
{
	int ret;

	no_other_options(ctx);

	if((ret = request_scan(ctx)) >= 0)
		goto got;
	else if(ret != -ENODEV)
		fail(NULL, NULL, ret);

	pick_scan_device(ctx);
got:
	wait_for_scan_results(ctx);
	fetch_dump_scan_list(ctx);
}

static void wait_for_connect(CTX)
{
	struct ucmsg* msg;
	int failures = 0;

	while((msg = recv_reply(ctx))) {
		switch(msg->cmd) {
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
					fail("no suitable APs", NULL, 0);
		}
	}
}

static int connect_stored(CTX, char* ssid)
{
	int slen = strlen(ssid);

	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_bin(UC, ATTR_SSID, ssid, slen);
	uc_put_end(UC);

	return send_recv_cmd(ctx);
}

static int connect_askpsk(CTX, char* ssid)
{
	int slen = strlen(ssid);

	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_bin(UC, ATTR_SSID, ssid, slen);
	put_psk_input(ctx, ssid, slen);
	uc_put_end(UC);

	return send_recv_cmd(ctx);
}

static void cmd_fixedap(CTX)
{
	char *ssid;
	int ret;
	
	if(!(ssid = shift_arg(ctx)))
		fail("need AP ssid", NULL, 0);

	no_other_options(ctx);

	ret = connect_stored(ctx, ssid);

	if(ret >= 0)
		goto got;
	if(ret == -ENOKEY)
		goto psk;
	if(ret != -ENODEV)
		goto err;

	pick_scan_device(ctx);
	warn("scanning", NULL, 0);
	wait_for_scan_results(ctx);

	ret = connect_stored(ctx, ssid);

	if(ret >= 0)
		goto got;
	if(ret != -ENOKEY)
		goto err;
psk:
	ret = connect_askpsk(ctx, ssid);

	if(ret >= 0)
		goto got;
err:
	fail(NULL, NULL, ret);
got:
	return wait_for_connect(ctx);
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

	send_check_cmd(ctx);
}

static void cmd_detach(CTX)
{
	no_other_options(ctx);

	uc_put_hdr(UC, CMD_WI_DETACH);
	uc_put_end(UC);

	send_check_cmd(ctx);
}

typedef void (*cmdptr)(CTX);

static const struct cmdrec {
	char name[12];
	cmdptr call;
} commands[] = {
	{ "device",     cmd_device  },
	{ "scan",       cmd_scan    },
	{ "ap",         cmd_fixedap },
	{ "connect",    cmd_fixedap },
	{ "dc",         cmd_neutral },
	{ "disconnect", cmd_neutral },
	{ "forget",     cmd_forget  },
	{ "bss",        cmd_bss     },
	{ "detach",     cmd_detach  }
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
	connect_to_wictl(ctx);

	if((cmd = shift_arg(ctx)))
		dispatch(ctx, cmd);
	else
		cmd_status(ctx);

	return 0;
}
