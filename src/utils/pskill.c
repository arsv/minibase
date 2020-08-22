#include <sys/signal.h>
#include <sys/dents.h>
#include <sys/file.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("kill");
ERRLIST(NEINVAL NEPERM NESRCH NENOSYS);

struct top {
	char** args;
	int argn;
	int fd;
	int pid;
	int len;
	int sig;

	char name[64];
};

#define CTX struct top* ctx

static const struct signame {
	int sig;
	char name[4];
} signames[] = {
#define SIG(a) { SIG##a, #a }
	SIG(HUP),
	SIG(INT),
	SIG(QUIT),
	SIG(ILL),
	SIG(ABRT),
	SIG(FPE),
	SIG(KILL),
	SIG(SEGV),
	SIG(PIPE),
	SIG(ALRM),
	SIG(TERM),
	SIG(USR1),
	SIG(USR2),
	SIG(CHLD),
	SIG(CONT),
	SIG(STOP),
	SIG(TSTP),
	SIG(TTIN),
	SIG(TTOU),
	{ 0, "" }
};

static char cmdbuf[4096];

static int sigbyname(char* name)
{
	const struct signame* sn;
	char* p;
	int sig;

	if((p = parseint(name, &sig)) && !*p)
		return sig;

	for(sn = signames; sn->sig; sn++)
		if(!strncmp(sn->name, name, 4))
			return sn->sig;

	fail("unknown signal", name, 0);
}

static void kill_proc(CTX, char* arg)
{
	int ret;

	if((ret = sys_kill(ctx->pid, ctx->sig)) < 0)
		warn("process", arg, ret);
}

static int find_by_pid(CTX, char* name)
{
	int i, n = ctx->argn;
	char** args = ctx->args;
	char* arg;

	for(i = 0; i < n; i++)
		if(!(arg = args[i]))
			continue;
		else if(!strcmp(arg, name))
			return i;

	return -1;
}

static int read_entry(CTX, int at, char* name)
{
	int fd, rd, ret;

	if((fd = sys_openat(at, name, O_RDONLY)) < 0)
		return fd;

	if((rd = sys_read(fd, cmdbuf, sizeof(cmdbuf))) > 0)
		ctx->len = rd;

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);

	return rd;
}

static int read_status(CTX, int at)
{
	int ret;

	if((ret = read_entry(ctx, at, "status")) < 0)
		return ret;

	if(ret < 6) /* Name:_ */
		return 0;
	if(strncmp(cmdbuf, "Name:", 5))
		return 0;

	char* p = cmdbuf + 5;
	char* e = cmdbuf + ret;

	if(*p++ != '\t')
		return 0;

	char* s = p;

	for(; p < e; p++)
		if(*p == '\n')
			break;
	if(p >= e)
		return 0;

	int len = p - s;

	if(len > sizeof(ctx->name))
		return 0;

	memzero(ctx->name, sizeof(ctx->name));
	memcpy(ctx->name, s, len);

	return len;
}

static int read_cmdline(CTX, int at)
{
	return read_entry(ctx, at, "cmdline");
}

static int find_by_name(CTX)
{
	int i, n = ctx->argn;
	char** args = ctx->args;
	char* arg;

	char* name = ctx->name;
	int size = sizeof(ctx->name);

	for(i = 0; i < n; i++)
		if(!(arg = args[i]))
			continue;
		else if(!strncmp(name, arg, size))
			return i;

	return -1;
}

static int find_by_cmd(CTX)
{
	int i, n = ctx->argn;
	char** args = ctx->args;
	char* arg;

	char* p = cmdbuf;
	char* e = cmdbuf + ctx->len;

	for(; p < e; p++)
		if(!*p) break;
	if(p >= e)
		return 0;

	char* s = p;
	char* c = cmdbuf;

	for(p = c; p < s; p++)
		if(*p == '/') c = p;

	char* cmd = c;
	int len = s - c;

	for(i = 0; i < n; i++)
		if(!(arg = args[i]))
			continue;
		else if(!strncmp(cmd, arg, len))
			return i;

	return -1;
}

static void read_proc(CTX, char* name)
{
	int at = ctx->fd;
	int fd = -1, ret;
	char* p;

	if(!(p = parseint(name, &ctx->pid)) || *p)
		return;
	if((ret = find_by_pid(ctx, name)) >= 0)
		goto got;
	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		return;

	if((ret = read_status(ctx, fd)) <= 0)
		goto out;
	if((ret = find_by_name(ctx)) >= 0)
		goto got;

	if((ret = read_cmdline(ctx, fd)) <= 0)
		goto out;
	if((ret = find_by_cmd(ctx)) < 0)
		goto out;
got:
	ctx->args[ret] = NULL;
	kill_proc(ctx, name);
out:
	if(fd >= 0) sys_close(fd);
}

static void report_remaining(CTX)
{
	int i, n = ctx->argn;
	char** args = ctx->args;
	char* arg;
	int left = 0;

	for(i = 0; i < n; i++) {
		if(!(arg = args[i]))
			continue;

		if(n > 1)
			warn("not found:", arg, 0);
		else
			warn("no matching processes", NULL, 0);

		left = 1;
	};

	if(left) _exit(0xFF);
}

static void kill_by_name(char** args, int n, int sig)
{
	struct top context, *ctx = &context;
	int fd, rd;
	char* dir = "/proc";
	char buf[2048];

	ctx->args = args;
	ctx->argn = n;
	ctx->sig = sig;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	ctx->fd = fd;

	while((rd = sys_getdents(ctx->fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;
			ptr += de->reclen;

			if(de->reclen <= 0)
				break;
			if(de->type != DT_DIR)
				continue;

			read_proc(ctx, de->name);
		}
	}

	report_remaining(ctx);
}

static void kill_numeric(char** args, int n, int sig)
{
	char* p;
	int pid, ret;

	for(int i = 0; i < n; i++) {
		char* arg = args[i];

		if(!(p = parseint(arg, &pid)) || *p)
			warn("invalid pid:", args[i], 0);
		else if((ret = sys_kill(pid, sig)) < 0)
			warn("process", arg, ret);
	}
}

static int only_numeric_pids(char** args, int n)
{
	char* p;
	int pid;

	for(int i = 0; i < n; i++)
		if(!(p = parseint(args[i], &pid)) || *p)
			return 0;

	return !0;
}

int main(int argc, char** argv)
{
	int i = 1;
	int sig = SIGTERM;

	if(i < argc && argv[i][0] == '-')
		sig = sigbyname(argv[i++] + 1);
	if(i >= argc)
		fail("too few arguments", NULL, 0);

	char** pspec = argv + i;
	int npspec = argc - i;

	if(only_numeric_pids(pspec, npspec))
		kill_numeric(pspec, npspec, sig);
	else
		kill_by_name(pspec, npspec, sig);

	return 0;
}
