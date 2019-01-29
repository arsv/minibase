#include <bits/socket/unix.h>
#include <bits/errno.h>

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/mman.h>

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

void* heap_alloc(CTX, uint size)
{
	void* brk = ctx->brk;
	void* ptr = ctx->ptr;

	if(!brk) {
		brk = sys_brk(NULL);
		ptr = brk;
	}
	
	ulong left = brk - ptr;

	if(left < size) {
		ulong need = pagealign(size - left);
		void* new = sys_brk(brk + need);

		if(brk_error(brk, new))
			fail("cannot allocate memory", NULL, 0);

		brk = new;
	}

	ctx->ptr = ptr + size;
	ctx->brk = brk;

	return ptr;
}

static void init_heap_bufs(CTX)
{
	char* ucbuf = heap_alloc(ctx, 2048);

	ctx->uc.brk = ucbuf;
	ctx->uc.ptr = ucbuf;
	ctx->uc.end = ucbuf + 2048;

	char* rxbuf = heap_alloc(ctx, 2048);

	ctx->ur.buf = rxbuf;
	ctx->ur.mptr = rxbuf;
	ctx->ur.rptr = rxbuf;
	ctx->ur.end = rxbuf + 2048;
}

int connect_to_wictl(CTX)
{
	int ret, fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = WICTL
	};

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_connect(fd, &addr, sizeof(addr))) < 0)
		fail("connect", addr.path, ret);

	ctx->fd = fd;
	ctx->connected = 1;

	init_heap_bufs(ctx);

	return ret;
}

void send_command(CTX)
{
	int wr, fd = ctx->fd;
	char* txbuf = ctx->uc.brk;
	int txlen = ctx->uc.ptr - ctx->uc.brk;

	if(!ctx->connected)
		fail("socket not connected", NULL, 0);

	if((wr = writeall(fd, txbuf, txlen)) < 0)
		fail("write", NULL, wr);
}

struct ucmsg* recv_reply(CTX)
{
	struct urbuf* ur = &ctx->ur;
	int ret, fd = ctx->fd;

	if((ret = uc_recv(fd, ur, 1)) < 0)
		return NULL;

	return ur->msg;
}

struct ucmsg* send_recv_msg(CTX)
{
	struct ucmsg* msg;

	send_command(ctx);

	while((msg = recv_reply(ctx)))
		if(msg->cmd <= 0)
			return msg;

	fail("connection lost", NULL, 0);
}

int send_recv_cmd(CTX)
{
	struct ucmsg* msg = send_recv_msg(ctx);

	return msg->cmd;
}

/* Command line args stuff */

static void init_context(CTX, int argc, char** argv)
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

	connect_to_wictl(ctx);

	uc_put_hdr(UC, CMD_WI_SETDEV);
	uc_put_str(UC, ATTR_NAME, name);
	uc_put_end(UC);

	send_check_cmd(ctx);
}

static void cmd_status(CTX)
{
	struct ucmsg* msg;

	connect_to_wictl(ctx);

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

	connect_to_wictl(ctx);

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

	uc_put_hdr(UC, CMD_WI_SETDEV);
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

	connect_to_wictl(ctx);

	uc_put_hdr(UC, CMD_WI_SCAN);
	uc_put_end(UC);

	if((ret = send_recv_cmd(ctx)) >= 0)
		goto got;
	else if(ret != -ENODEV)
		fail(NULL, NULL, ret);

	pick_scan_device(ctx);
got:
	wait_for_scan_results(ctx);
	fetch_dump_scan_list(ctx);
}

static void check_ap_in_range(CTX)
{
	struct ucmsg* msg;
	int setdev = 0;
again:
	uc_put_hdr(UC, CMD_WI_STATUS);
	uc_put_end(UC);

	msg = send_recv_msg(ctx);

	if(msg->cmd < 0)
		fail(NULL, NULL, msg->cmd);

	if(!uc_get_int(msg, ATTR_IFI)) {
		if(setdev)
			fail("lost active device", NULL, 0);

		pick_scan_device(ctx);
		wait_for_scan_results(ctx);
		setdev = 1;

		goto again;
	}

	/* TODO: go through the APs, choose which ciphers to use */
}

static void connect_and_wait(CTX)
{
	struct ucmsg* msg;
	int ret, failures = 0;

	uc_put_hdr(UC, CMD_WI_CONNECT);
	uc_put_bin(UC, ATTR_SSID, ctx->ssid, ctx->slen);
	uc_put_bin(UC, ATTR_PSK, ctx->psk, sizeof(ctx->psk));
	uc_put_end(UC);

	if((ret = send_recv_cmd(ctx)) < 0)
		fail(NULL, NULL, ret);

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

static void shift_ssid_arg(CTX)
{
	char *ssid;
	
	if(!(ssid = shift_arg(ctx)))
		fail("need AP ssid", NULL, 0);

	int slen = strlen(ssid);

	if(slen > 32)
		fail("SSID too long", NULL, 0);

	memcpy(ctx->ssid, ssid, slen);
	ctx->slen = slen;
}

static void cmd_fixedap(CTX)
{
	shift_ssid_arg(ctx);
	no_other_options(ctx);

	connect_to_wictl(ctx);

	check_ap_in_range(ctx);
	load_or_ask_psk(ctx);

	connect_and_wait(ctx);

	maybe_store_psk(ctx);
}

static void cmd_saved(CTX)
{
	no_other_options(ctx);

	list_saved_psks(ctx);
}

static void cmd_forget(CTX)
{
	shift_ssid_arg(ctx);
	no_other_options(ctx);

	remove_psk_entry(ctx);
}

static void cmd_detach(CTX)
{
	no_other_options(ctx);

	connect_to_wictl(ctx);

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
	{ "detach",     cmd_detach  },
	{ "scan",       cmd_scan    },
	{ "ap",         cmd_fixedap },
	{ "connect",    cmd_fixedap },
	{ "dc",         cmd_neutral },
	{ "disconnect", cmd_neutral },
	{ "saved",      cmd_saved   },
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
	init_context(ctx, argc, argv);

	if((cmd = shift_arg(ctx)))
		dispatch(ctx, cmd);
	else
		cmd_status(ctx);

	return 0;
}
