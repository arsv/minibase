#include <sys/file.h>
#include <sys/dents.h>
#include <sys/mman.h>
#include <sys/creds.h>

#include <errtag.h>
#include <format.h>
#include <printf.h>
#include <string.h>
#include <output.h>
#include <util.h>

ERRTAG("pslist");

#define OPTS "rc"
#define OPT_r (1<<0)
#define OPT_c (1<<1)

struct top {
	int self;
	int opts;

	void* brk;
	void* ptr;
	void* end;

	int npatt;
	char** patts;

	char state;
	int pid, ppid;
	int uid, euid;
	int gid, egid;

	struct bufout bo;
};

static char outbuf[4096];

#define CTX struct top* ctx __unused

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

static void format_proc_status(CTX)
{
	FMTBUF(p, e, buf, 50);
	p = fmtstr(p, e, "\033[33m");
	p = fmtint(p, e, ctx->pid);
	p = fmtstr(p, e, "\033[0m");
	FMTEND(p, e);

	output(ctx, buf, p - buf);
}

static void out_no_cmd(CTX)
{
	char* str = " (no command)";
	int len = strlen(str);

	return output(ctx, str, len);
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

	output(ctx, "\"", 1);

	for(; p < end; p++) {
		char c = *p;

		if(c == '"' || c == '$') {
			output(ctx, "\\", 1);
			output(ctx, p, 1);
		} else if(c == '\n') {
			output(ctx, "\\n", 2);
		} else if(c == '\t') {
			output(ctx, "\\t", 2);
		} else {
			output(ctx, p, 1);
		}
	}

	output(ctx, "\"", 1);
}

static void out_one_arg(CTX, char* arg, int len)
{
	output(ctx, " ", 1);

	if(needs_quoting(arg, len))
		out_quoted(ctx, arg, len);
	else
		output(ctx, arg, len);
}

static void format_proc_cmdline(CTX)
{
	char* p = ctx->brk;
	char* e = ctx->ptr;
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

	output(ctx, "\n", 1);
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
	int needstate = ctx->opts & OPT_r;

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
		else if(needstate && !strcmp(key, "State"))
			ctx->state = *val;
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

static int check_proc_info(CTX)
{
	if(ctx->pid < 0 || ctx->ppid < 0)
		return -1;
	if(ctx->pid == 2 || ctx->ppid == 2)
		return -1;
	if(ctx->pid == ctx->self)
		return -1;

	if((ctx->opts & OPT_r) && ctx->state != 'R')
		return -1;

	return 0;
}

static int check_proc_cmdline(CTX)
{
	int i, n = ctx->npatt;
	char** patts = ctx->patts;

	if(!n) return 0;

	char* cmd = ctx->brk;
	char* end = ctx->ptr;
	int len = end - cmd;

	if(ctx->opts & OPT_c)
		len = strnlen(cmd, len);

	for(i = 0; i < n; i++)
		if(strnstr(cmd, patts[i], len))
			return 0;

	return -1;
}

static void reset_proc_data(CTX)
{
	ctx->state = 0;

	ctx->pid = -1;
	ctx->ppid = -1;

	ctx->ptr = ctx->brk;
}

static int read_proc_cmdline(CTX, int at)
{
	int fd, rd;
	void* buf = ctx->ptr;
	int len = ctx->end - buf;

	if((fd = sys_openat(at, "cmdline", O_RDONLY)) < 0)
		return fd;

	while(1) {
		rd = sys_read(fd, buf, len);

		if(rd > 0)
			ctx->ptr += rd;
		if(rd < len)
			break;

		void* old = ctx->end;
		void* new = sys_brk(old + PAGE);

		if((rd = brk_error(old, new)) < 0)
			break;

		ctx->end = new;
		buf = ctx->ptr;
		len = new - buf;
	}

	if(rd < 0)
		ctx->ptr = ctx->brk;

	sys_close(fd);

	return rd;
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
	if(check_proc_cmdline(ctx))
		goto out;

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
		fail(NULL, dir, fd);

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

static void init_cmdmem(CTX)
{
	void* brk = sys_brk(NULL);
	void* end = sys_brk(brk + PAGE);

	if(brk_error(brk, end))
		fail("cannot initialize heap", NULL, 0);

	ctx->brk = brk;
	ctx->ptr = brk;
	ctx->end = end;
}

static void init_output(CTX)
{
	ctx->bo.fd = STDOUT;
	ctx->bo.buf = outbuf;
	ctx->bo.len = sizeof(outbuf);
}

static void fini_output(CTX)
{
	bufoutflush(&ctx->bo);
}

int main(int argc, char** argv)
{
	int i = 1;
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		ctx->opts = argbits(OPTS, argv[i++] + 1);

	ctx->self = sys_getpid();
	ctx->npatt = argc - i;
	ctx->patts = argv + i;

	init_cmdmem(ctx);
	init_output(ctx);

	read_proc_list(ctx);

	fini_output(ctx);

	return 0;
}
