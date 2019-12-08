#include <bits/socket/unix.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <nlusctl.h>
#include <util.h>
#include <main.h>

#include "common.h"

ERRTAG("svctl");

struct top {
	int opts;
	int argc;
	int argi;
	char** argv;

	int fd;

	char smallbuf[128];

	void* brk;
	void* ptr;
	void* end;
};

#define CTX struct top* ctx

void* heap_alloc(CTX, int size)
{
	void* brk;
	void* end;
	void* ptr;

	if(!(brk = ctx->brk)) {
		brk = sys_brk(NULL);
		ptr = brk;
		end = brk;
		ctx->brk = brk;
	} else {
		ptr = ctx->ptr;
		end = ctx->end;
	}

	void* newptr = ptr + size;

	if(newptr > end) {
		long need = pagealign(newptr - end);
		void* newend = sys_brk(end + need);

		if(newptr > newend)
			fail("brk", NULL, -ENOMEM);

		ctx->end = newend;
	}

	ctx->ptr = newptr;

	return ptr;
}

static void heap_trim(CTX, void* to)
{
	void* brk = ctx->brk;
	void* ptr = ctx->ptr;

	if(to < brk || to > ptr)
		fail("invalid trim", NULL, 0);

	ctx->ptr = to;
}

/* output */

static int cmp_proc(const void* a, const void* b)
{
	struct ucattr* at = *((struct ucattr**)a);
	struct ucattr* bt = *((struct ucattr**)b);

	char* sa = uc_get_str(at, ATTR_NAME);
	char* sb = uc_get_str(bt, ATTR_NAME);

	if(!sa || !sb)
		return 0;

	return strcmp(sa, sb);
}

static void index_procs(CTX, void* ptr, void* end)
{
	while(ptr < end) {
		struct ucattr* msg = ptr;
		int msglen = msg->len;

		if(msglen <= 0) break;

		ptr += msg->len;

		struct ucattr* at;

		for(at = uc_get_0(msg); at; at = uc_get_n(msg, at)) {
			struct ucattr** p;

			p = heap_alloc(ctx, sizeof(*p));

			*p = at;
		}
	}
}

static int intlen(int x)
{
	int len = 1;

	if(x < 0) { x = -x; len++; };

	for(; x >= 10; len++) x /= 10;

	return len;
}

static int pid_len(int maxlen, struct ucattr* p)
{
	int *pid, len;

	if(!(pid = uc_get_int(p, ATTR_PID)))
		;
	else if((len = intlen(*pid)) > maxlen)
		maxlen = len;

	return maxlen;
}

static void dump_proc(struct bufout* bo, struct ucattr* at, int maxlen)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;
	char* q;

	char* name = uc_get_str(at, ATTR_NAME);
	int* pid = uc_get_int(at, ATTR_PID);

	if(pid)
		q = fmtint(p, e, *pid);
	else
		q = fmtstr(p, e, "-");

	p = fmtpad(p, e, maxlen, q);

	if(uc_get(at, ATTR_RING))
		p = fmtstr(p, e, "*");
	else
		p = fmtstr(p, e, " ");

	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, name ? name : "???");

	*p++ = '\n';

	bufout(bo, buf, p - buf);
}

static void dump_list(CTX, void* ptr, void* end)
{
	struct ucattr** procs = ctx->ptr;

	index_procs(ctx, ptr, end);

	struct ucattr** prend = ctx->ptr;

	qsort(procs, prend - procs, sizeof(*procs), cmp_proc);

	struct ucattr** p;
	int maxlen = 0;
	struct bufout bo;
	char output[2048];

	bufoutset(&bo, STDOUT, output, sizeof(output));

	for(p = procs; p < prend; p++)
		maxlen = pid_len(maxlen, *p);

	for(p = procs; p < prend; p++)
		dump_proc(&bo, *p, maxlen);

	bufoutflush(&bo);
}

static void dump_pid(CTX, struct ucattr* msg)
{
	int* pid;

	if(!(pid = uc_get_int(msg, ATTR_PID)))
		fail("no PID in reply", NULL, 0);

	FMTBUF(p, e, buf, 50);
	p = fmtint(p, e, *pid);
	FMTENL(p, e);

	writeall(STDOUT, buf, p - buf);
}

/* arguments */

static void init_context(CTX, int argc, char** argv)
{
	if(argc > 1 && argv[1][0] == '-')
		fail("no top-level options allowed", NULL, 0);

	ctx->argi = 1;
	ctx->argc = argc;
	ctx->argv = argv;

	ctx->fd = -1;
}

static void no_other_options(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
}

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		fail("too few arguments", NULL, 0);

	return ctx->argv[ctx->argi++];
}

/* socket stuff */

static void send_request(CTX, struct ucbuf* uc)
{
	int ret, fd;
	char* path = CONTROL;

	if((fd = ctx->fd) >= 0)
		goto send;

	if((fd = sys_socket(AF_UNIX, SOCK_SEQPACKET, 0)) < 0)
		fail("socket", NULL, fd);
	if((ret = uc_connect(fd, path)) < 0)
		fail("connect", path, ret);

	ctx->fd = fd;
send:
	if((ret = uc_send(fd, uc)) < 0)
		fail("send", NULL, ret);
}

static struct ucattr* recv_reply(CTX)
{
	int ret, fd = ctx->fd;
	void* buf = ctx->smallbuf;
	int len = sizeof(ctx->smallbuf);
	struct ucattr* msg;

	if((ret = uc_recv(fd, buf, len)) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(buf, ret)))
		fail("recv", NULL, -EBADMSG);

	return msg;
}

static struct ucattr* recv_large(CTX)
{
	int len = 2*PAGE;
	int ret, fd = ctx->fd;
	void* buf = heap_alloc(ctx, len);
	struct ucattr* msg;

	if((ret = uc_recv(fd, buf, len)) < 0)
		fail("recv", NULL, ret);
	if(!(msg = uc_msg(buf, ret)))
		fail("recv", NULL, -EBADMSG);

	heap_trim(ctx, buf + len);

	int rep;

	if((rep = uc_repcode(msg)) < 0)
		fail(NULL, NULL, rep);
	else if(rep > 0)
		fail("unexpected notification", NULL, 0);

	return msg;
}

static void recv_empty_reply(CTX)
{
	struct ucattr* msg = recv_reply(ctx);
	int rep = uc_repcode(msg);

	if(rep < 0) fail(NULL, NULL, rep);
}

static void simple_void_cmd(CTX, int cmd)
{
	char buf[128];
	struct ucbuf uc;

	no_other_options(ctx);

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);

	send_request(ctx, &uc);

	recv_empty_reply(ctx);
}

static void send_proc_cmd(CTX, int cmd, char* name)
{
	char buf[128];
	struct ucbuf uc;

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);
	uc_put_str(&uc, ATTR_NAME, name);

	send_request(ctx, &uc);
}

static void simple_proc_cmd(CTX, int cmd)
{
	char* name = shift_arg(ctx);

	no_other_options(ctx);

	send_proc_cmd(ctx, cmd, name);

	recv_empty_reply(ctx);
}

/* individual commands */

static void cmd_start(CTX)
{
	simple_proc_cmd(ctx, CMD_START);
}

static void cmd_stop(CTX)
{
	simple_proc_cmd(ctx, CMD_STOP);
}

static void cmd_restart(CTX)
{
	char* name = shift_arg(ctx);
	struct ucattr* msg;
	int rep;

	send_proc_cmd(ctx, CMD_STOP, name);
	msg = recv_reply(ctx);

	if((rep = uc_repcode(msg)) == 0)
		return;
	if(rep > 0)
		fail("unexpected notification", NULL, 0);
	if(rep != -EALREADY)
		fail(NULL, NULL, rep);

	send_proc_cmd(ctx, CMD_START, name);
	recv_empty_reply(ctx);
}

static void cmd_hup(CTX)
{
	simple_proc_cmd(ctx, CMD_HUP);
}

static void cmd_pause(CTX)
{
	simple_proc_cmd(ctx, CMD_PAUSE);
}

static void cmd_resume(CTX)
{
	simple_proc_cmd(ctx, CMD_RESUME);
}

static void cmd_flush(CTX)
{
	simple_proc_cmd(ctx, CMD_FLUSH);
}

static void cmd_pidof(CTX)
{
	char* name = shift_arg(ctx);

	send_proc_cmd(ctx, CMD_STATUS, name);

	struct ucattr* msg = recv_reply(ctx);

	dump_pid(ctx, msg);
}

static void cmd_dump(CTX)
{
	char* name = shift_arg(ctx);

	send_proc_cmd(ctx, CMD_GETBUF, name);

	struct ucattr* msg = recv_large(ctx);

	writeall(STDOUT, uc_payload(msg), uc_paylen(msg));
}

static void cmd_list(CTX)
{
	int start = 0;
	char buf[128];
	struct ucbuf uc;
	struct ucattr* msg;

	no_other_options(ctx);

	uc_buf_set(&uc, buf, sizeof(buf));
next:
	uc_put_hdr(&uc, CMD_LIST);
	uc_put_int(&uc, ATTR_NEXT, start);

	send_request(ctx, &uc);

	msg = recv_large(ctx);

	int* next = uc_get_int(msg, ATTR_NEXT);

	if(!next) goto done;

	start = *next;
	goto next;
done:
	dump_list(ctx, ctx->brk, ctx->ptr);
}

static void cmd_reload(CTX)
{
	simple_void_cmd(ctx, CMD_RELOAD);
}

static void cmd_reboot(CTX)
{
	simple_void_cmd(ctx, CMD_REBOOT);
}

static void cmd_shutdown(CTX)
{
	simple_void_cmd(ctx, CMD_SHUTDOWN);
}

static void cmd_poweroff(CTX)
{
	simple_void_cmd(ctx, CMD_POWEROFF);
}

static void cmd_flushall(CTX)
{
	simple_void_cmd(ctx, CMD_FLUSH);
}

static const struct cmdrec {
	char name[12];
	void (*cmd)(CTX);
} commands[] = {
	{ "hup",       cmd_hup      },
	{ "pidof",     cmd_pidof    },
	{ "pause",     cmd_pause    },
	{ "restart",   cmd_restart  },
	{ "start",     cmd_start    },
	{ "stop",      cmd_stop     },
	{ "flush",     cmd_flush    },
	{ "flush-all", cmd_flushall },
	{ "resume",    cmd_resume   },
	{ "reload",    cmd_reload   },
	{ "reboot",    cmd_reboot   },
	{ "shutdown",  cmd_shutdown },
	{ "poweroff",  cmd_poweroff },
	{ "dump",      cmd_dump     }
};

typedef void (*cmdptr)(CTX);

static cmdptr resolve(char* name)
{
	const struct cmdrec* p;

	for(p = commands; p < commands + ARRAY_SIZE(commands); p++)
		if(!strncmp(p->name, name, sizeof(p->name)))
			return p->cmd;

	fail("unknown command", name, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	memzero(&context, sizeof(context));
	cmdptr handler;

	init_context(ctx, argc, argv);

	if(argc > 1)
		handler = resolve(shift_arg(ctx));
	else
		handler = cmd_list;

	handler(ctx);

	return 0;
}
