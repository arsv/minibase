#include <bits/socket/unix.h>
#include <bits/errno.h>
#include <sys/file.h>
#include <sys/socket.h>

#include <errtag.h>
#include <nlusctl.h>
#include <output.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "common.h"

ERRTAG("ifctl");
ERRLIST(NENOENT NEINVAL NENOSYS NENOENT NEACCES NEPERM NEBUSY NEALREADY
	NENETDOWN NENOKEY NENOTCONN NENODEV NETIMEDOUT);

#define OPTS "adxw"
#define OPT_a (1<<0)
#define OPT_d (1<<1)
#define OPT_x (1<<2)
#define OPT_w (1<<3)

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	int fd;
	struct ucbuf uc;
	struct urbuf ur;
	int connected;
	char txbuf[64];
	char rxbuf[512];

	struct bufout bo;
};

typedef struct ucattr* attr;
#define CTX struct top* ctx
#define MSG struct ucmsg* msg
#define AT struct ucattr* at
#define UC (&ctx->uc)

void init_socket(CTX)
{
	int fd;

	ctx->uc.brk = ctx->txbuf;
	ctx->uc.ptr = ctx->txbuf;
	ctx->uc.end = ctx->txbuf + sizeof(ctx->txbuf);

	ctx->ur.buf = ctx->rxbuf;
	ctx->ur.mptr = ctx->rxbuf;
	ctx->ur.rptr = ctx->rxbuf;
	ctx->ur.end = ctx->rxbuf + sizeof(ctx->rxbuf);

	if((fd = sys_socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
		fail("socket", "AF_UNIX", fd);

	ctx->fd = fd;
}

static void connect_socket(CTX)
{
	int ret;

	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = IFCTL
	};

	if((ret = sys_connect(ctx->fd, &addr, sizeof(addr))) < 0)
		fail("connect", addr.path, ret);

	ctx->connected = 1;
}

void send_command(CTX)
{
	int wr, fd = ctx->fd;
	char* txbuf = ctx->uc.brk;
	int txlen = ctx->uc.ptr - ctx->uc.brk;

	if(!ctx->connected)
		connect_socket(ctx);

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

static struct ucmsg* send_recv_msg(CTX)
{
	struct ucmsg* msg;

	send_command(ctx);

	while((msg = recv_reply(ctx)))
		if(msg->cmd <= 0)
			return msg;

	fail("connection lost", NULL, 0);
}

static int send_recv_cmd(CTX)
{
	struct ucmsg* msg = send_recv_msg(ctx);

	return msg->cmd;
}

static void send_check(CTX)
{
	int ret;

	if((ret = send_recv_cmd(ctx)) < 0)
		fail(NULL, NULL, ret);
}

static void no_other_options(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("bad options", NULL, 0);
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

static int shift_ifi(CTX)
{
	char* name;
	int ifi;
	
	if(!(name = shift_arg(ctx)))
		fail("interface name required", NULL, 0);

	if((ifi = getifindex(ctx->fd, name)) < 0)
		fail(NULL, name, ifi);

	return ifi;
}

static void req_status(CTX)
{
	fail("not implemented:", __FUNCTION__, 0);
}

static void stop_link_wait(CTX, int ifi)
{
	struct ucmsg* msg;
	int ret;

	uc_put_hdr(UC, CMD_IF_STOP);
	uc_put_int(UC, ATTR_IFI, ifi);
	uc_put_end(UC);

	if((ret = send_recv_cmd(ctx)) == -EALREADY)
		return;
	else if(ret < 0)
		fail(NULL, NULL, ret);

	while((msg = recv_reply(ctx)))
		if(msg->cmd == REP_IF_LINK_STOP)
			break;
		else if(msg->cmd == REP_IF_LINK_GONE)
			fail("link gone", NULL, 0);
}

static void req_neutral(CTX)
{
	int ifi = shift_ifi(ctx);
	no_other_options(ctx);

	stop_link_wait(ctx, ifi);
}

static void req_set_link(CTX, int cmd)
{
	int ifi = shift_ifi(ctx);
	no_other_options(ctx);

	stop_link_wait(ctx, ifi);

	uc_put_hdr(UC, cmd);
	uc_put_int(UC, ATTR_IFI, ifi);
	uc_put_end(UC);

	send_check(ctx);
}

static void req_skip(CTX)
{
	return req_set_link(ctx, CMD_IF_SET_SKIP);
}

static void req_wifi(CTX)
{
	return req_set_link(ctx, CMD_IF_SET_WIFI);
}

//static void wait_for_connect(CTX)
//{
//	struct ucmsg* msg;
//	int failures = 0;
//
//	while((msg = recv_reply(ctx))) switch(msg->cmd) {
//		case REP_WI_SCANNING:
//			warn("scanning", NULL, 0);
//			break;
//		case REP_WI_SCAN_FAIL:
//			fail("scan failed", NULL, 0);
//		case REP_WI_NET_DOWN:
//			fail(NULL, NULL, -ENETDOWN);
//		case REP_WI_CONNECTED:
//			warn_sta("connected to", msg);
//			return;
//		case REP_WI_DISCONNECT:
//			warn_sta("cannot connect to", msg);
//			failures++;
//			break;
//		case REP_WI_NO_CONNECT:
//			if(failures)
//				fail("no more APs in range", NULL, 0);
//			else
//				fail("no suitable APs in range", NULL, 0);
//	}
//
//	check_not_null(msg);
//}

static void req_restart(CTX)
{
	int ifi = shift_ifi(ctx);

	no_other_options(ctx);

	uc_put_hdr(UC, CMD_IF_RESTART);
	uc_put_int(UC, ATTR_IFI, ifi);
	uc_put_end(UC);

	send_check(ctx);
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
	init_socket(ctx);

	if(use_opt(ctx, OPT_d))
		req_neutral(ctx);
	else if(use_opt(ctx, OPT_a))
		req_restart(ctx);
	else if(use_opt(ctx, OPT_x))
		req_skip(ctx);
	else if(use_opt(ctx, OPT_w))
		req_wifi(ctx);
	else
		req_status(ctx);

	return 0;
}
