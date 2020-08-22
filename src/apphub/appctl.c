#include <bits/socket/unix.h>
#include <bits/ioctl/socket.h>
#include <bits/errno.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/proc.h>

#include <nlusctl.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <main.h>
#include <netlink.h>
#include <output.h>

#include "common.h"
#include "appctl.h"

ERRTAG("appctl");

#define SMALL_REQUEST 1024

static void connect_socket(CTX)
{
	int ret, fd;
	struct sockaddr_un addr = {
		.family = AF_UNIX,
		.path = CONTROL
	};

	if((fd = sys_socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0)
		fail("socket", "AF_UNIX", fd);
	if((ret = sys_connect(fd, &addr, sizeof(addr))) < 0)
		fail(NULL, addr.path, ret);

	ctx->fd = fd;
}

static void recv_simple_reply(CTX)
{
	int ret, fd = ctx->fd;
	char buf[128];
	struct ucattr* msg;

	if((ret = uc_recv(fd, buf, sizeof(buf))) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(buf, ret)))
		fail("recv:", "invalid message", 0);

	if((ret = uc_repcode(msg)) < 0)
		fail(NULL, NULL, ret);
	else if(ret > 0)
		fail("unexpected notification", NULL, 0);
}

static void* alloc_heap(int size)
{
	int aligned = pagealign(size);

	void* buf = sys_brk(NULL);
	void* end = sys_brk(buf + aligned);

	if(end - buf != aligned)
		fail("brk", NULL, -ENOMEM);

	return buf;
}

static struct ucattr* recv_large(CTX, void* buf, int size)
{
	int ret, fd = ctx->fd;
	struct ucattr* msg;

	if((ret = uc_recv(fd, buf, size)) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(buf, ret)))
		fail("invalid message", NULL, 0);

	return msg;
}

static void send_request(CTX, struct ucbuf* uc)
{
	connect_socket(ctx);

	int ret, fd = ctx->fd;

	if((ret = uc_send(fd, uc)) < 0)
		fail("send", NULL, ret);
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
		fail("too few arguments", NULL, 0);

	return ctx->argv[ctx->argi++];
}

int got_more_options(CTX)
{
	return (ctx->argi < ctx->argc);
}

static char** argv_left(CTX)
{
	return ctx->argv + ctx->argi;
}

static int argc_left(CTX)
{
	return ctx->argc - ctx->argi;
}

static char* fmt_status(char* p, char* e, int status)
{
	if(WIFEXITED(status)) {
		p = fmtstr(p, e, "exit ");
		p = fmtint(p, e, WEXITSTATUS(status));
	} else if(WIFSIGNALED(status)) {
		p = fmtstr(p, e, "signal ");
		p = fmtint(p, e, WTERMSIG(status));
	} else {
		p = fmtstr(p, e, "status ");
		p = fmtpad0(p, e, 4, fmthex(p, e, status));
	}

	return p;
}

static void dump_proc(CTX, struct bufout* bo, struct ucattr* at)
{
	char* name = uc_get_str(at, ATTR_NAME);
	int* xid = uc_get_int(at, ATTR_XID);
	int* pid = uc_get_int(at, ATTR_PID);
	int* exit = uc_get_int(at, ATTR_EXIT);
	int* ring = uc_get_int(at, ATTR_RING);

	FMTBUF(p, e, buf, 64);

	if(xid)
		p = fmtpadr(p, e, 3, fmtint(p, e, *xid));
	else
		p = fmtstr(p, e, " - ");

	if(name)
		p = fmtstr(p, e, name);
	else
		p = fmtstr(p, e, "???");

	p = fmtstr(p, e, "  (");

	if(pid) {
		p = fmtstr(p, e, "pid ");
		p = fmtint(p, e, *pid);
	} else if(exit) {
		p = fmt_status(p, e, *exit);
	}

	p = fmtstr(p, e, ")");

	if(ring)
		p = fmtstr(p, e, " *");

	FMTENL(p, e);

	bufout(bo, buf, p - buf);
}

static void req_status(CTX)
{
	char buf[128];
	struct ucattr *msg, *at;
	struct ucbuf uc;
	struct bufout bo;
	int start = 0, *next;

	no_other_options(ctx);

	void* heap = alloc_heap(3*PAGE);
	void* obuf = heap;
	int osize = PAGE;
	void* mbuf = heap + 2*PAGE;
	int msize = 2*PAGE;

	bufoutset(&bo, STDOUT, obuf, osize);
again:
	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, CMD_STATUS);
	uc_put_int(&uc, ATTR_NEXT, start);
	send_request(ctx, &uc);

	msg = recv_large(ctx, mbuf, msize);

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at)) {
		if(uc_is_keyed(at, ATTR_PROC))
			dump_proc(ctx, &bo, at);
	}
	if((next = uc_get_int(msg, ATTR_NEXT))) {
		start = *next;
		goto again;
	}

	bufoutflush(&bo);
}

static int estimate_request_size(CTX)
{
	char** args = argv_left(ctx);
	int i, argn = argc_left(ctx);

	int sum = 0;
	int count = 0;

	for(i = 0; i < argn; i++) {
		char* arg = args[i];
		int len = strlen(arg);
		len = (len + 3) & ~3;
		sum += len;
		count++;
	}

	char** envp = ctx->environ;
	char* evar;

	while((evar = *envp++)) {
		int len = strlen(evar);
		len = (len + 3) & ~3;
		sum += len;
		count++;
	}

	return 64 + 4*count + sum;
}

static void spawn_request(CTX, UC)
{
	char** argv = argv_left(ctx);
	int argc = argc_left(ctx);
	char *var, **envp = ctx->environ;
	struct ucattr* at;

	uc_put_hdr(uc, CMD_SPAWN);

	at = uc_put_strs(uc, ATTR_ARGV);
	for(int i = 0; i < argc; i++)
		uc_add_str(uc, argv[i]);
	uc_end_strs(uc, at);

	at = uc_put_strs(uc, ATTR_ENVP);
	while((var = *envp++))
		uc_add_str(uc, var);
	uc_end_strs(uc, at);

	send_request(ctx, uc);

	recv_simple_reply(ctx);
}

static void req_spawn(CTX)
{
	int est = estimate_request_size(ctx);
	struct ucbuf uc;

	if(est <= SMALL_REQUEST) {
		char buf[SMALL_REQUEST+20];

		uc_buf_set(&uc, buf, sizeof(buf));

		spawn_request(ctx, &uc);
	} else {
		char* buf = sys_brk(NULL);
		char* end = sys_brk(buf + pagealign(est));
		long len = end - buf;

		uc_buf_set(&uc, buf, len);

		spawn_request(ctx, &uc);
	}
}

static int shift_xid(CTX)
{
	char* p;
	int xid;

	char* arg = shift_arg(ctx);

	no_other_options(ctx);

	if(!(p = parseint(arg, &xid)) || *p)
		fail("invalid xid:", arg, 0);

	return xid;
}

static void send_xid_request(CTX, int cmd, int xid)
{
	char txbuf[128];
	struct ucbuf uc;

	uc_buf_set(&uc, txbuf, sizeof(txbuf));
	uc_put_hdr(&uc, cmd);
	uc_put_int(&uc, ATTR_XID, xid);

	send_request(ctx, &uc);
}

static void req_sigterm(CTX)
{
	int xid = shift_xid(ctx);

	no_other_options(ctx);

	send_xid_request(ctx, CMD_SIGTERM, xid);

	recv_simple_reply(ctx);
}

static void req_sigkill(CTX)
{
	int xid = shift_xid(ctx);

	no_other_options(ctx);

	send_xid_request(ctx, CMD_SIGKILL, xid);

	recv_simple_reply(ctx);
}

static void req_fetch(CTX)
{
	int xid = shift_xid(ctx);

	no_other_options(ctx);

	int size = 2*PAGE;
	void* buf = alloc_heap(size);

	send_xid_request(ctx, CMD_FETCH, xid);

	struct ucattr* msg = recv_large(ctx, buf, size);

	writeall(STDOUT, uc_payload(msg), uc_paylen(msg));
}

static void req_flush(CTX)
{
	int xid = shift_xid(ctx);

	no_other_options(ctx);

	send_xid_request(ctx, CMD_FLUSH, xid);

	recv_simple_reply(ctx);
}

static const struct cmd {
	char name[16];
	void (*call)(CTX);
} cmds[] = {
	{ "spawn",   req_spawn    },
	{ "sigterm", req_sigterm  },
	{ "sigkill", req_sigkill  },
	{ "show",    req_fetch    },
	{ "flush",   req_flush    }
};

static void invoke(CTX)
{
	const struct cmd* cc;

	if(!got_more_options(ctx))
		return req_status(ctx);

	char* cmd = shift_arg(ctx);

	for(cc = cmds; cc < ARRAY_END(cmds); cc++)
		if(!strncmp(cmd, cc->name, sizeof(cc->name)))
			return cc->call(ctx);

	fail("unknown command", cmd, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->argi = 1;

	ctx->environ = argv + argc + 1;

	ctx->fd = -1;

	invoke(ctx);

	return 0;
}
