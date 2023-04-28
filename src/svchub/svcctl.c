#include <bits/socket/unix.h>
#include <sys/reboot.h>
#include <sys/socket.h>
#include <sys/proc.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <nlusctl.h>
#include <util.h>
#include <main.h>

#include "common.h"

ERRTAG("svcctl");

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

#define CTX struct top* ctx __unused

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

static int cmp_proc(void* a, void* b)
{
	struct ucattr* at = a;
	struct ucattr* bt = b;

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

static void dump_proc(struct bufout* bo, struct ucattr* at)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	char* name = uc_get_str(at, ATTR_NAME);
	int* pid = uc_get_int(at, ATTR_PID);
	int* exit = uc_get_int(at, ATTR_EXIT);

	p = fmtstr(p, e, name ? name : "???");

	if(pid) {
		p = fmtstr(p, e, " (pid ");
		p = fmtint(p, e, *pid);
		p = fmtstr(p, e, ")");
	} else if(exit) {
		int status = *exit;

		if(WIFEXITED(status)) {
			p = fmtstr(p, e, " (exit ");
			p = fmtint(p, e, WEXITSTATUS(status));
			p = fmtstr(p, e, ")");
		} else if(WIFSIGNALED(status)) {
			p = fmtstr(p, e, " (signal ");
			p = fmtint(p, e, WTERMSIG(status));
			p = fmtstr(p, e, ")");
		}
	} else {
		p = fmtstr(p, e, " (stopped)");
	}

	if(uc_get(at, ATTR_RING))
		p = fmtstr(p, e, " *");

	*p++ = '\n';

	bufout(bo, buf, p - buf);
}

static void dump_list(CTX, void* ptr, void* end)
{
	struct ucattr** procs = ctx->ptr;

	index_procs(ctx, ptr, end);

	struct ucattr** prend = ctx->ptr;

	qsortp(procs, prend - procs, cmp_proc);

	struct ucattr** p;
	struct bufout bo;
	char output[2048];

	bufoutset(&bo, STDOUT, output, sizeof(output));

	for(p = procs; p < prend; p++)
		dump_proc(&bo, *p);

	bufoutflush(&bo);
}

static void dump_proc_status(CTX, struct ucattr* msg)
{
	int *pid, *ex;

	FMTBUF(p, e, buf, 100);

	if((pid = uc_get_int(msg, ATTR_PID))) {
		p = fmtstr(p, e, "Running, pid ");
		p = fmtint(p, e, *pid);
	} else if((ex = uc_get_int(msg, ATTR_EXIT))) {
		int status = *ex;
		p = fmtstr(p, e, "Dead");
		if(WIFEXITED(status)) {
			p = fmtstr(p, e, ", exit ");
			p = fmtint(p, e, WEXITSTATUS(status));
		} else if(WIFSIGNALED(status)) {
			p = fmtstr(p, e, ", signal ");
			p = fmtint(p, e, WTERMSIG(status));
		}
	} else {
		p = fmtstr(p, e, "Status unknown");
	}

	FMTENL(p, e);
	writeall(STDOUT, buf, p - buf);
}

/* In the status output, we want nice complete lines.
   The full buf is also too large for a short status
   report, so we truncate it to a reasonable size. */

static char* skip_to_start(char* buf, int len)
{
	int qlen = 512;

	if(len <= qlen)
		return buf;

	char* p = buf + len - qlen;

	while(p > buf) {
		if(*p == '\n')
			break;
		p--;
	}

	return p;
}

static void dump_proc_output(CTX, struct ucattr* msg)
{
	char* buf = uc_payload(msg);
	int len = uc_paylen(msg);
	char* end = buf + len;

	if(!len) return;

	char* from = skip_to_start(buf, len);
	char* last = end - 1;

	if(from == buf)
		writeall(STDOUT, "\n", 1);

	writeall(STDOUT, from, end - from);

	if(*last != '\n')
		writeall(STDOUT, "\n", 1);
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

static void recv_empty_reply(CTX, char* name)
{
	struct ucattr* msg = recv_reply(ctx);
	int rep = uc_repcode(msg);

	if(rep < 0)
		fail(NULL, name, rep);
	if(rep > 0)
		fail("unexpected notification", NULL, 0);
}

static void simple_void_cmd(CTX, int cmd)
{
	char buf[128];
	struct ucbuf uc;

	no_other_options(ctx);

	uc_buf_set(&uc, buf, sizeof(buf));
	uc_put_hdr(&uc, cmd);

	send_request(ctx, &uc);

	recv_empty_reply(ctx, NULL);
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

	recv_empty_reply(ctx, name);
}

static void wait_notification(CTX)
{
	struct ucattr* msg = recv_reply(ctx);
	int rep = uc_repcode(msg);

	if(rep != REP_DIED)
		fail("unexptected notification", NULL, 0);
}

/* individual commands */

static void cmd_start(CTX)
{
	simple_proc_cmd(ctx, CMD_START);
}

static void cmd_spawn(CTX)
{
	simple_proc_cmd(ctx, CMD_SPAWN);
}

static void cmd_stout(CTX)
{
	simple_proc_cmd(ctx, CMD_STOUT);
}

static void cmd_stop(CTX)
{
	simple_proc_cmd(ctx, CMD_STOP);
	wait_notification(ctx);
}

static void cmd_restart(CTX)
{
	char* name = shift_arg(ctx);
	struct ucattr* msg;
	int rep;

	send_proc_cmd(ctx, CMD_STOP, name);

	msg = recv_reply(ctx);
	rep = uc_repcode(msg);

	if(rep == -EAGAIN) {
		/* not running, start it */
		send_proc_cmd(ctx, CMD_START, name);
	} else if(rep < 0) {
		/* does not exist, or cannot restart */
		fail(NULL, name, rep);
	} else if(rep == 0) {
		/* success, wait for it to die */
		wait_notification(ctx);
		send_proc_cmd(ctx, CMD_START, name);
	} else {
		fail("unexpected notification", NULL, 0);
	}
}

static void cmd_remove(CTX)
{
	char* name = shift_arg(ctx);
	struct ucattr* msg;
	int rep;

	send_proc_cmd(ctx, CMD_STOP, name);

	msg = recv_reply(ctx);
	rep = uc_repcode(msg);

	if(rep == -EAGAIN) {
		/* not running, can remove instantly */
		send_proc_cmd(ctx, CMD_REMOVE, name);
	} else if(rep < 0) {
		/* does not exist, or cannot restart */
		fail(NULL, name, rep);
	} else if(rep == 0) {
		/* success, wait for it to die */
		wait_notification(ctx);
		send_proc_cmd(ctx, CMD_REMOVE, name);
	} else {
		fail("unexpected notification", NULL, 0);
	}
}

static void cmd_sighup(CTX)
{
	simple_proc_cmd(ctx, CMD_SIGHUP);
}

static void cmd_flush(CTX)
{
	simple_proc_cmd(ctx, CMD_FLUSH);
}

static void cmd_status(CTX)
{
	struct ucattr* msg;
	char* name = shift_arg(ctx);
	int rep;

	send_proc_cmd(ctx, CMD_STATUS, name);

	msg = recv_reply(ctx);

	if((rep = uc_repcode(msg)) < 0)
		fail(NULL, NULL, rep);

	dump_proc_status(ctx, msg);

	if(!uc_get(msg, ATTR_RING))
		return;

	send_proc_cmd(ctx, CMD_GETBUF, name);

	msg = recv_large(ctx);

	dump_proc_output(ctx, msg);
}

static void cmd_output(CTX)
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

static const struct cmdrec {
	char name[12];
	void (*cmd)(CTX);
} commands[] = {
	{ "start",     cmd_start    },
	{ "spawn",     cmd_spawn    },
	{ "stout",     cmd_stout    },
	{ "stop",      cmd_stop     },
	{ "restart",   cmd_restart  },
	{ "remove",    cmd_remove   },
	{ "flush",     cmd_flush    },
	{ "sighup",    cmd_sighup   },

	{ "status",    cmd_status   },
	{ "output",    cmd_output   },

	{ "reboot",    cmd_reboot   },
	{ "shutdown",  cmd_shutdown },
	{ "poweroff",  cmd_poweroff },
};

typedef void (*cmdptr)(CTX);

static cmdptr resolve(char* name)
{
	const struct cmdrec* p;

	for(p = commands; p < commands + ARRAY_SIZE(commands); p++)
		if(!strcmpn(p->name, name, sizeof(p->name)))
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
