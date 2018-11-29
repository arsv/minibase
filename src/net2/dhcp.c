#include <bits/socket/unix.h>
#include <bits/errno.h>
#include <sys/file.h>
#include <sys/socket.h>

#include <nlusctl.h>
#include <output.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "common.h"

ERRTAG("dhcp");
ERRLIST(NENOENT NEINVAL NENOSYS NENOENT NEACCES NEPERM NEBUSY NEALREADY
	NENETDOWN NENOKEY NENOTCONN NENODEV NETIMEDOUT);

#define OPTS "andxwqr"
#define OPT_a (1<<0)
#define OPT_n (1<<1)
#define OPT_d (1<<2)
#define OPT_x (1<<3)
#define OPT_w (1<<4)
#define OPT_q (1<<5)
#define OPT_r (1<<6)

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

	int ifi;
	char* ifname;

	int retry;
};

typedef struct ucattr* attr;
#define CTX struct top* ctx __unused
#define MSG struct ucmsg* msg __unused
#define AT struct ucattr* at __unused
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

/* Link list output */

//static int count_links(MSG)
//{
//	struct ucattr* at;
//	int count = 0;
//
//	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
//		if(uc_is_nest(at, ATTR_LINK))
//			count++;
//
//	return count;
//}
//
//static void fill_links(MSG, struct ucattr** idx, int n)
//{
//	struct ucattr* at;
//	int i = 0;
//
//	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
//		if(i >= n)
//			break;
//		else if(!uc_is_nest(at, ATTR_LINK))
//			continue;
//		else
//			idx[i++] = at;
//}
//
//static const char* modes[] = {
//	[IF_MODE_SKIP] = "skip",
//	[IF_MODE_DOWN] = "down",
//	[IF_MODE_DHCP] = "dhcp",
//	[IF_MODE_WIFI] = "wifi"
//};
//
//static char* fmt_mode(char* p, char* e, uint mode)
//{
//	if(mode < ARRAY_SIZE(modes))
//		return fmtstr(p, e, modes[mode]);
//	else
//		return fmtint(p, e, mode);
//}
//
//static char* fmt_flags(char* p, char* e, int flags)
//{
//	p = fmtstr(p, e, "(");
//
//	if(flags & IF_CARRIER)
//		p = fmtstr(p, e, "carrier");
//	else if(flags & IF_ENABLED)
//		p = fmtstr(p, e, "up");
//	else
//		p = fmtstr(p, e, "down");
//
//	if(flags & IF_STOPPING)
//		p = fmtstr(p, e, ",stopping");
//	else if(flags & IF_RUNNING)
//		p = fmtstr(p, e, ",running");
//
//	if(flags & IF_ERROR)
//		p = fmtstr(p, e, ",error");
//	if(flags & IF_DHCPFAIL)
//		p = fmtstr(p, e, ",dhcp-fail");
//
//	p = fmtstr(p, e, ")");
//
//	return p;
//}
//
//static char* fmt_link(char* p, char* e, struct ucattr* at)
//{
//	int* ifi = uc_sub_int(at, ATTR_IFI);
//	char* name = uc_sub_str(at, ATTR_NAME);
//	int* mode = uc_sub_int(at, ATTR_MODE);
//	int* flags = uc_sub_int(at, ATTR_FLAGS);
//	byte* addr = uc_sub_bin(at, ATTR_ADDR, 6);
//
//	if(!ifi || !name || !mode || !flags || !addr)
//		return p;
//
//	p = fmtmac(p,e , addr);
//	p = fmtstr(p, e, " #");
//	p = fmtint(p, e, *ifi);
//	p = fmtstr(p, e, " ");
//	p = fmtstr(p, e, name);
//	p = fmtstr(p, e, ": ");
//	p = fmt_mode(p, e, *mode);
//	p = fmtstr(p, e, " ");
//	p = fmt_flags(p, e, *flags);
//	p = fmtstr(p, e, "\n");
//
//	return p;
//}
//
//static void dump_status(CTX, MSG)
//{
//	int i, n = count_links(msg);
//	struct ucattr* idx[n];
//	fill_links(msg, idx, n);
//
//	FMTBUF(p, e, buf, 2048);
//
//	for(i = 0; i < n; i++)
//		p = fmt_link(p, e, idx[i]);
//
//	FMTEND(p, e);
//
//	writeall(STDOUT, buf, p - buf);
//}

/* Wire utils */

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

/* Cmdline arguments */

static void no_other_options(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
	if(ctx->opts)
		fail("bad options", NULL, 0);
}

//static int use_opt(CTX, int opt)
//{
//	int ret = ctx->opts & opt;
//	ctx->opts &= ~opt;
//	return ret;
//}

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		return NULL;

	return ctx->argv[ctx->argi++];
}

static void req_drop(CTX)
{
	fail("not supported", NULL, 0);
}

static void req_release(CTX)
{
	fail("not supported", NULL, 0);
}

static void req_renew(CTX)
{
	fail("not supported", NULL, 0);
}

static void req_leases(CTX)
{
	no_other_options(ctx);

	uc_put_hdr(UC, CMD_IF_LEASES);
	uc_put_end(UC);

	send_check(ctx);
}

static void req_start(CTX)
{
	fail("not supported", NULL, 0);
}

static const struct cmd {
	char name[16];
	void (*call)(CTX);
} cmds[] = {
	{ "leases",    req_leases  },
	{ "list",      req_leases  },
	{ "start",     req_start   },
	{ "request",   req_start   },
	{ "renew",     req_renew   },
	{ "release",   req_release },
	{ "drop",      req_drop    }
};

int invoke(CTX, const struct cmd* cc)
{
	init_socket(ctx);

	cc->call(ctx);

	return 0;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	const struct cmd* cc; 

	memzero(ctx, sizeof(*ctx));

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->argi = 1;

	char* lead = shift_arg(ctx);

	if(!lead)
		return invoke(ctx, &cmds[0]);
	if(*lead == '-')
		fail("no options allowed", NULL, 0);

	for(cc = cmds; cc < cmds + ARRAY_SIZE(cmds); cc++)
		if(!strncmp(cc->name, lead, sizeof(cc->name)))
			return invoke(ctx, cc);

	ctx->argi--;
	req_start(ctx);

	return 0;
}
