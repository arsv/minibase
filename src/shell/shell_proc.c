#include <sys/file.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <printf.h>

#include "shell.h"

#define DIRBUF 2048
#define CMDBUF 512
#define OUTBUF 1024

struct proccontext {
	void* dirbuf;
	void* cmdbuf;
	int cmdlen;

	int state;

	int pid, ppid;
	int uid, euid;
	int gid, egid;

	struct bufout bo;
};

#define CTX struct proccontext* ctx

static void proc_output(CTX, void* buf, int len)
{
	struct bufout* bo = &ctx->bo;

	bufout(bo, buf, len);
}

static void format_proc_status(CTX)
{
	char buf[20];

	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtint(p, e, ctx->pid);

	proc_output(ctx, buf, p - buf);
}

static void out_no_cmd(CTX)
{
	char* str = " (no command)";
	int len = strlen(str);

	proc_output(ctx, str, len);
}

static int needs_quoting(char* arg, int len)
{
	char* end = arg + len;
	char* p = arg;

	for(; p < end; p++) {
		char c = *p;

		if(c == ' ' || c == '\t' || c == '\n')
			return 1;
		else if(c == '\'' || c == '"')
			return 1;
	}

	return 0;
}

static void out_quoted(CTX, char* arg, int len)
{
	char* end = arg + len;
	char* p = arg;

	proc_output(ctx, "\"", 1);

	for(; p < end; p++) {
		char c = *p;

		if(c == '"' || c == '$') {
			proc_output(ctx, "\\", 1);
			proc_output(ctx, p, 1);
		} else if(c == '\n') {
			proc_output(ctx, "\\n", 2);
		} else if(c == '\t') {
			proc_output(ctx, "\\t", 2);
		} else {
			proc_output(ctx, p, 1);
		}
	}

	proc_output(ctx, "\"", 1);
}

static void out_one_arg(CTX, char* arg, int len)
{
	proc_output(ctx, " ", 1);

	if(needs_quoting(arg, len))
		out_quoted(ctx, arg, len);
	else
		proc_output(ctx, arg, len);
}

static void format_proc_cmdline(CTX)
{
	char* p = ctx->cmdbuf;
	char* e = p + ctx->cmdlen;
	char* s = p;

	while(e > p && !*(e-1))
		e--;

	if(p >= e)
		out_no_cmd(ctx);

	for(; p < e; p++) {
		if(*p) continue;
		out_one_arg(ctx, s, p - s);
		s = p + 1;
	} if(p > s) {
		out_one_arg(ctx, s, p - s);
	}

	proc_output(ctx, "\n", 1);
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* skipspace(char* p, char* e)
{
	for(; p < e; p++)
		if(!isspace(*p))
			break;

	return p;
}

static void set_int(char* p, int* dst)
{
	(void)parseint(p, dst);
}

static void set_uids(char* p, int* uid, int* euid)
{
	if(!(p = parseint(p, uid)))
		return;
	while(*p && isspace(*p))
		p++;

	(void)parseint(p, euid);
}

static int parse_status(CTX, char* buf, int len)
{
	char* end = buf + len;
	char* ls;
	char* le;

	for(ls = buf; ls < end; ls = le + 1) {
		if((le = strecbrk(ls, end, '\n')) >= end)
			break;

		char* key = ls;
		char* val;

		if((val = strecbrk(ls, le, ':')) >= le)
			break;

		*le = '\0'; *val++ = '\0';
		val = skipspace(val, le);

		if(!strcmp(key, "Pid"))
			set_int(val, &ctx->pid);
		else if(!strcmp(key, "PPid"))
			set_int(val, &ctx->ppid);
		else if(!strcmp(key, "Uid"))
			set_uids(val, &ctx->uid, &ctx->euid);
		else if(!strcmp(key, "Gid"))
			set_uids(val, &ctx->gid, &ctx->egid);
	}

	return 0;
}

static int read_proc_status(CTX, int at)
{
	char buf[512];
	int fd, rd;

	if((fd = sys_openat(at, "status", O_RDONLY)) < 0)
		return fd;
	if((rd = sys_read(fd, buf, sizeof(buf))) < 0)
		return rd;

	sys_close(fd);

	return parse_status(ctx, buf, rd);
}

static int read_proc_cmdline(CTX, int at)
{
	int fd, rd;
	char* buf = ctx->cmdbuf;
	int len = CMDBUF;

	if((fd = sys_openat(at, "cmdline", O_RDONLY)) < 0)
		return fd;

	if((rd = sys_read(fd, buf, len)) >= 0)
		ctx->cmdlen = rd;

	sys_close(fd);

	return rd;
}

static int check_proc_info(CTX)
{
	if(ctx->pid < 0 || ctx->ppid < 0)
		return -1;
	if(ctx->pid == 2 || ctx->ppid == 2)
		return -1;

	return 0;
}

static void reset_proc_data(CTX)
{
	ctx->cmdlen = 0;

	ctx->state = 0;

	ctx->pid = -1;
	ctx->ppid = -1;

	//ctx->ptr = ctx->brk;
}

static void read_proc(CTX, int at, char* pidstr)
{
	int fd;

	if((fd = sys_openat(at, pidstr, O_DIRECTORY)) < 0)
		return;

	reset_proc_data(ctx);

	if(read_proc_status(ctx, fd) < 0)
		goto out;
	if(check_proc_info(ctx))
		goto out;
	if(read_proc_cmdline(ctx, fd) < 0)
		goto out;
	//if(check_proc_cmdline(ctx, pidstr))
	//	goto out;

	format_proc_status(ctx);
	format_proc_cmdline(ctx);
out:
	sys_close(fd);
}

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

static void read_proc_list(CTX)
{
	int fd, rd;
	char* dir = "/proc";
	char buf[2048];

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		return repl(NULL, dir, fd);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;
			ptr += de->reclen;

			if(de->reclen <= 0)
				break;
			if(de->type != DT_DIR)
				continue;
			if(!isdigit(de->name[0]))
				continue;

			read_proc(ctx, fd, de->name);
		}
	}
}

static void prep_context(CTX)
{
	memzero(ctx, sizeof(*ctx));

	ctx->dirbuf = heap_alloc(DIRBUF);
	ctx->cmdbuf = heap_alloc(CMDBUF);

	void* outbuf = heap_alloc(OUTBUF);

	bufoutset(&ctx->bo, STDOUT, outbuf, OUTBUF);
}

static void fini_context(CTX)
{
	bufoutflush(&ctx->bo);
}

void cmd_ps(void)
{
	struct proccontext c, *ctx = &c;

	prep_context(ctx);
	read_proc_list(ctx);
	fini_context(ctx);
	//repl("not implemented", NULL, 0);
}

void cmd_kill(void)
{
	repl("not implemented", NULL, 0);
}
