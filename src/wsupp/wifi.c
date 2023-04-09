#include <bits/socket/unix.h>
#include <bits/errno.h>

#include <sys/file.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "wifi.h"

ERRTAG("wifi");
ERRLIST(NENOENT NEINVAL NENOSYS NENOENT NEACCES NEPERM NEBUSY NEALREADY
	NENETDOWN NENOKEY NENOTCONN NENODEV NETIMEDOUT NENOBUFS NEISCONN
	NEAGAIN);

static void connect_to_wictl(CTX)
{
	int ret, fd;
	char* path = CONTROL;

	if((fd = sys_socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = uc_connect(fd, path)) < 0)
		fail("connect", path, ret);

	ctx->fd = fd;
}

static void send_request(CTX, struct ucbuf* uc)
{
	int wr, fd = ctx->fd;

	if((wr = uc_send(fd, uc)) < 0)
		fail("send", NULL, wr);
}

static struct ucattr* recv_reply(CTX)
{
	int ret, fd = ctx->fd;
	void* buf = ctx->rxbuf;
	int len = sizeof(ctx->rxbuf);
	struct ucattr* msg;

	if((ret = uc_recv(fd, buf, len)) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(buf, ret)))
		fail("recv", NULL, -EBADMSG);

	return msg;
}

static int recv_empty_reply(CTX)
{
	struct ucattr* msg = recv_reply(ctx);

	return uc_repcode(msg);
}

struct ucattr* send_recv_msg(CTX, struct ucbuf* uc)
{
	send_request(ctx, uc);

	return recv_reply(ctx);
}

/* Command line args stuff */

static void init_context(CTX, int argc, char** argv)
{
	ctx->argi = 1;
	ctx->argc = argc;
	ctx->argv = argv;
	ctx->fd = -1;
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

static int simple_request(CTX, int cmd)
{
	struct ucbuf uc;
	char buf[128];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);

	send_request(ctx, &uc);

	return recv_empty_reply(ctx);
}

static int setdev_request(CTX, int ifi)
{
	struct ucbuf uc;
	char buf[128];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, CMD_SETDEV);
	uc_put_int(&uc, ATTR_IFI, ifi);

	send_request(ctx, &uc);

	return recv_empty_reply(ctx);
}

static struct ucattr* fetch_status(CTX)
{
	struct ucbuf uc;
	struct ucattr* msg;
	char buf[128];
	int ret;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, CMD_STATUS);

	msg = send_recv_msg(ctx, &uc);

	if((ret = uc_repcode(msg)) < 0)
		fail(NULL, NULL, ret);

	return msg;
}

/* User commands */

static void cmd_device(CTX)
{
	char *name;
	int ret, ifi;

	if(!(name = shift_arg(ctx)))
		fail("need device name", NULL, 0);

	no_other_options(ctx);

	/* This is suboptimal, but we try to connect first and
	   resolve device name after that. Not worth the trouble
	   messing with the socket to resolve the name first. */
	connect_to_wictl(ctx);

	if((ifi = getifindex(ctx->fd, name)) < 0)
		fail(NULL, name, ifi);

	if((ret = setdev_request(ctx, ifi)) < 0)
		fail(NULL, NULL, ret);
}

static void cmd_status(CTX)
{
	struct ucattr* msg;

	no_other_options(ctx);

	connect_to_wictl(ctx);

	msg = fetch_status(ctx);

	fetch_scan_list(ctx);

	dump_status(ctx, msg);
}

static void cmd_neutral(CTX)
{
	connect_to_wictl(ctx);

	no_other_options(ctx);

	int ret = simple_request(ctx, CMD_NEUTRAL);

	if(ret == -ECANCELED)
		return;
	if(ret < 0)
		fail(NULL, NULL, ret);
	if(ret > 0)
		fail("unexpected notification", NULL, 0);

	struct ucattr* msg;

	while((msg = recv_reply(ctx)))
		if(uc_repcode(msg) == REP_DISCONNECT)
			break;
}

static void resume_service(CTX)
{
	int ret;

	if((ret = simple_request(ctx, CMD_RESUME)) < 0)
		fail(NULL, NULL, ret);
}

static void cmd_resume(CTX)
{
	no_other_options(ctx);
	connect_to_wictl(ctx);

	resume_service(ctx);
}

static void wait_for_scan_results(CTX)
{
	struct ucattr* msg;
	int* err;

	while((msg = recv_reply(ctx)))
		if(uc_repcode(msg) == REP_SCAN_END)
			break;

	if((err = uc_get_int(msg, ATTR_ERROR)) && *err)
		fail(NULL, NULL, *err);
}

static void set_scan_device(CTX, int ifi, char* name)
{
	int ret;

	if((ret = setdev_request(ctx, ifi)) < 0)
		fail(NULL, name, ret);
}

static void pick_scan_device(CTX)
{
	char name[32];
	int ifi;

	find_wifi_device(name);

	if((ifi = getifindex(ctx->fd, name)) < 0)
		fail(NULL, name, ifi);

	set_scan_device(ctx, ifi, name);
}

static void cmd_scan(CTX)
{
	no_other_options(ctx);

	connect_to_wictl(ctx);

	int ret = simple_request(ctx, CMD_RUNSCAN);

	if(ret >= 0)
		goto got;
	else if(ret != -ENODEV)
		fail(NULL, NULL, ret);

	pick_scan_device(ctx);
got:
	wait_for_scan_results(ctx);

	fetch_scan_list(ctx);
	dump_scan_list(ctx);
}

/* When connecting to a network we don't have PSK for,
   make sure it's range before asking the user for passphrase. */

static void check_suitable_aps(CTX)
{
	struct ucattr** scans = ctx->scans;
	int i, n = ctx->count;
	int seen = 0;

	for(i = 0; i < n; i++) {
		struct ucattr* at = scans[i];
		struct ucattr* ies = uc_get(at, ATTR_IES);

		if(!ies) continue;

		int can = can_use_ap(ctx, ies);

		if(can == AP_CANCONN)
			return;
		if(can == AP_NOCRYPT)
			seen = 1;
	}

	if(seen)
		fail("unsupported encryption mode", NULL, 0);
	else
		fail("no scan results for this SSID", NULL, 0);
}

static void check_ap_available(CTX)
{
	fetch_scan_list(ctx);
	check_suitable_aps(ctx);
}

static int need_resume(MSG)
{
	int* stp = uc_get_int(msg, ATTR_STATE);

	if(!stp) return 0;

	int state = *stp;

	if(state == 0) /* OP_STOPPED */
		return 1;
	if(state == 30) /* OP_NETDOWN */
		return 1;
	if(state == 31) /* OP_EXTERNAL */
		return 1;

	return 0;
}

static void prep_scan_results(CTX)
{
	struct ucattr* msg;

	msg = fetch_status(ctx);

	if(uc_get(msg, ATTR_BSSID))
		fail("already connected", NULL, 0);

	if(!uc_get_int(msg, ATTR_IFI)) {
		pick_scan_device(ctx);
		wait_for_scan_results(ctx);
	} else if(need_resume(msg)) {
		resume_service(ctx);
		wait_for_scan_results(ctx);
	}
}

static void wait_for_connect(CTX)
{
	struct ucattr* msg;
	int failures = 0;

	while((msg = recv_reply(ctx))) {
		int rep = uc_repcode(msg);

		if(rep == REP_LINK_READY) {
			warn_sta(ctx, "connected to", msg);
			return;
		} else if(rep == REP_DISCONNECT) {
			warn_bss(ctx, "cannot connect to", msg);
			failures++;
			break;
		} else if(rep == REP_NO_CONNECT) {
			if(failures > 1)
				warn("no more APs in range", NULL, 0);
			else if(!failures)
				warn("no suitable APs", NULL, 0);
			_exit(0xFF);
		}
	}
}

static int send_conn_request(CTX)
{
	struct ucbuf uc;
	char buf[128];

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, CMD_CONNECT);
	uc_put_bin(&uc, ATTR_SSID, ctx->ssid, ctx->slen);
	uc_put_bin(&uc, ATTR_PSK, ctx->psk, sizeof(ctx->psk));

	send_request(ctx, &uc);

	return recv_empty_reply(ctx);
}

/* When connecting with a saved PSK, no STATUS request is made
   so the service may happen to be in the idle state. */

static void start_conn_scan(CTX)
{
	int ret;

	if((ret = send_conn_request(ctx)) >= 0)
		return;
	if(ret == -ENODEV) {
		pick_scan_device(ctx);
		wait_for_scan_results(ctx);
	} else if(ret == -ENONET) {
		resume_service(ctx);
		wait_for_scan_results(ctx);
	} else {
		goto err;
	}

	if((ret = send_conn_request(ctx)) >= 0)
		return;
err:
	fail(NULL, NULL, ret);

}

/* Connections without PSK always start with a STATUS request
   which should initialize the device, so no point in doing it
   here. */

static void start_connection(CTX)
{
	int ret;

	if((ret = send_conn_request(ctx)) < 0)
		fail(NULL, NULL, ret);
}

/* TODO: SSID parsing.

   SSIDs are not guaranteed to be nice strings, may be almost
   arbitrary byte sequences, up to 32 long. */

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

static void cmd_connect(CTX)
{
	shift_ssid_arg(ctx);
	no_other_options(ctx);

	connect_to_wictl(ctx);

	if(load_saved_psk(ctx)) {
		start_conn_scan(ctx);
		wait_for_connect(ctx);
	} else {
		prep_scan_results(ctx);
		check_ap_available(ctx);
		ask_passphrase(ctx);
		start_connection(ctx);
		wait_for_connect(ctx);
		maybe_store_psk(ctx);
	}
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
	int ret;

	no_other_options(ctx);

	connect_to_wictl(ctx);

	if((ret = simple_request(ctx, CMD_DETACH)) < 0)
		fail(NULL, NULL, ret);
}

typedef void (*cmdptr)(CTX);

static const struct cmdrec {
	char name[12];
	cmdptr call;
} commands[] = {
	{ "device",     cmd_device  },
	{ "detach",     cmd_detach  },
	{ "scan",       cmd_scan    },
	{ "ap",         cmd_connect },
	{ "connect",    cmd_connect },
	{ "dc",         cmd_neutral },
	{ "disconnect", cmd_neutral },
	{ "resume",     cmd_resume  },
	{ "saved",      cmd_saved   },
	{ "forget",     cmd_forget  },
};

static void dispatch(CTX, char* name)
{
	const struct cmdrec* r;

	for(r = commands; r < commands + ARRAY_SIZE(commands); r++)
		if(!strcmpn(r->name, name, sizeof(r->name)))
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
