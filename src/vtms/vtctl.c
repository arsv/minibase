#include <bits/socket/unix.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <nlusctl.h>
#include <util.h>
#include <main.h>

#include "common.h"

ERRTAG("vtctl");
ERRLIST(NENOENT NECONNREFUSED NELOOP NENFILE NEMFILE NEINTR
	NEINVAL NEACCES NEPERM NEIO NEFAULT NENOSYS);

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	int fd;

	char buf[128];
};

#define CTX struct top* ctx
#define MSG struct ucattr* msg

#define OPTS "sbku"
#define OPT_s (1<<0)
#define OPT_b (1<<1)
#define OPT_k (1<<2)
#define OPT_u (1<<3)

static int open_proc_entry(int pid, char* key)
{
	FMTBUF(p, e, buf, 40);
	p = fmtstr(p, e, "/proc/");
	p = fmtint(p, e, pid);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, key);
	FMTEND(p, e);

	return sys_open(buf, O_RDONLY);
}

static char* maybe_put_comm(char* p, char* e, int pid)
{
	int fd, rd;
	char buf[50];
	char* q;

	if((fd = open_proc_entry(pid, "comm")) < 0)
		return p;

	if((rd = sys_read(fd, buf, sizeof(buf))) > 0) {
		q = fmtraw(p, e, buf, rd);
		if(q > p && *(q-1) == '\n') q--;
		p = q;
	}

	sys_close(fd);

	return p;
}

static void show_vt(CTX, struct bufout* bo, struct ucattr* vt, int active)
{
	int* tty = uc_get_int(vt, ATTR_TTY);
	int* pid = uc_get_int(vt, ATTR_PID);

	if(!tty) return;

	FMTBUF(p, e, buf, 100);

	char* q = p;

	q = fmtstr(q, e, "tty");
	q = fmtint(p, e, *tty);
	q = fmtstr(q, e, (*tty == active ? "*" : ""));
	p = fmtpadr(p, e, 6, q);

	p = fmtstr(p, e, "  ");
	q = pid ? fmtint(p, e, *pid) : fmtstr(p, e, "-");
	p = fmtpadr(p, e, 5, q);

	p = fmtstr(p, e, "  ");

	p = maybe_put_comm(p, e, *pid);

	FMTENL(p, e);

	bufout(bo, buf, p - buf);
}

static int cmpvt(const void* a, const void* b)
{
	struct ucattr* at = *((struct ucattr**)a);
	struct ucattr* bt = *((struct ucattr**)b);

	int* pa = uc_get_int(at, ATTR_TTY);
	int* pb = uc_get_int(bt, ATTR_TTY);

	int ttya = pa ? *pa : 0;
	int ttyb = pb ? *pb : 0;

	if(ttya < ttyb)
		return -1;
	if(ttya > ttyb)
		return  1;

	return 0;
}

static int index_vts(MSG, struct ucattr** dst, int max)
{
	struct ucattr* at;
	int i, nvts = 0;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(uc_is_keyed(at, ATTR_VT))
			nvts++;

	if(nvts > max)
		fail("too many VTs to show", NULL, 0);

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(uc_is_keyed(at, ATTR_VT))
			dst[i++] = at;

	qsort(dst, nvts, sizeof(void*), cmpvt);

	return nvts;
}

static int fetch_active(MSG)
{
	int* p = uc_get_int(msg, ATTR_TTY);

	return p ? *p : -1;
}

void dump_status(CTX, MSG)
{
	struct bufout bo;
	char output[1024];
	struct ucattr* vts[32];

	bufoutset(&bo, STDOUT, output, sizeof(output));

	int active = fetch_active(msg);
	int i, n = index_vts(msg, vts, ARRAY_SIZE(vts));

	for(i = 0; i < n; i++)
		show_vt(ctx, &bo, vts[i], active);

	bufoutflush(&bo);
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
	if(ctx->argi < ctx->argc)
		return ctx->argv[ctx->argi++];
	else
		return NULL;
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

	ctx->fd = -1;
}

static void send_request(CTX, struct ucbuf* uc)
{
	int ret, fd;
	char* path = CONTROL;

	if((fd = ctx->fd) > 0)
		goto send;

	if((fd = sys_socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = uc_connect(fd, path)) < 0)
		fail(NULL, path, ret);
send:
	if((ret = uc_send(fd, uc)) < 0)
		fail("send", NULL, ret);
}

static struct ucattr* recv_reply(CTX)
{
	int ret, fd = ctx->fd;
	void* buf = ctx->buf;
	int len = sizeof(ctx->buf);
	struct ucattr* msg;

	if((ret = uc_recv(fd, buf, len)) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(buf, ret)))
		fail("recv", NULL, -EBADMSG);

	return msg;
}

static void recv_empty(CTX)
{
	struct ucattr* msg;
	int rep;

	msg = recv_reply(ctx);

	if((rep = uc_repcode(msg)) < 0)
		fail(NULL, NULL, rep);
	else if(rep > 0)
		fail("unexpected notification", NULL, 0);
}

static void cmd_status(CTX)
{
	char buf[64];
	struct ucbuf uc;
	struct ucattr* msg;

	no_other_options(ctx);

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, CMD_STATUS);

	send_request(ctx, &uc);

	msg = recv_reply(ctx);

	dump_status(ctx, msg);
}

static int parse_tty(char* str)
{
	char* p;
	int tty;

	if(!(p = parseint(str, &tty)) || *p)
		fail("bad tty id", str, 0);

	return tty;
}

static void cmd_switch(CTX, char* id)
{
	char buf[128];
	struct ucbuf uc;
	int tty = parse_tty(id);

	no_other_options(ctx);

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, CMD_SWITCH);
	uc_put_int(&uc, ATTR_TTY, tty);

	send_request(ctx, &uc);

	recv_empty(ctx);
}

static void simple_command(CTX, int cmd)
{
	char buf[128];
	struct ucbuf uc;

	no_other_options(ctx);

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);

	send_request(ctx, &uc);

	recv_empty(ctx);
}

static void cmd_swback(CTX)
{
	simple_command(ctx, CMD_SWBACK);
}

static void cmd_swlock(CTX)
{
	simple_command(ctx, CMD_SWLOCK);
}

static void cmd_unlock(CTX)
{
	simple_command(ctx, CMD_UNLOCK);
}

static const struct cmdrec {
	char name[8];
	void (*cmd)(CTX);
} commands[] = {
	{ "back",   cmd_swback },
	{ "lock",   cmd_swlock },
	{ "unlock", cmd_unlock }
};

static void dispatch(CTX, char* name)
{
	const struct cmdrec* cr;

	if(*name >= '0' && *name <= '9')
		return cmd_switch(ctx, name);

	for(cr = commands; cr < commands + ARRAY_SIZE(commands); cr++)
		if(!strncmp(cr->name, name, sizeof(cr->name)))
			return cr->cmd(ctx);

	fail("unknown command", name, 0);
}

int main(int argc, char** argv)
{
	char* name;

	struct top context, *ctx = &context;
	memzero(&context, sizeof(context));

	init_args(ctx, argc, argv);

	if(!(name = shift_arg(ctx)))
		cmd_status(ctx);
	else
		dispatch(ctx, name);

	return 0;
}
