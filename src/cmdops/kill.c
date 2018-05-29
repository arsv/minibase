#include <sys/file.h>
#include <sys/dents.h>
#include <sys/creds.h>
#include <sys/signal.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("kill");

#define OPTS "khsc"
#define OPT_k (1<<0)
#define OPT_h (1<<1)
#define OPT_s (1<<2)
#define OPT_c (1<<3)

struct top {
	int opts;
	int sig;

	int ncmd;
	char** cmds;
	int npid;
	char** pids;
	
	int killed;
	int failed;
};

#define CTX struct top* ctx

static int read_proc_file(char* buf, int len, int at, char* pstr, char* name)
{
	int fd, rd;

	FMTBUF(p, e, path, 40);
	p = fmtstr(p, e, pstr);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	if((fd = sys_openat(at, path, O_RDONLY)) < 0)
		return fd;
	if((rd = sys_read(fd, buf, len)) < 0)
		return rd;

	sys_close(fd);

	return rd;
}

static int slashes(char* cmd)
{
	char* p;

	for(p = cmd; *p; p++)
		if(*p == '/')
			return 1;
	
	return 0;
}

static int match_proc_name(CTX, int at, char* pstr)
{
	char buf[128];
	int len;

	if((len = read_proc_file(buf, sizeof(buf), at, pstr, "status")) < 0)
		return 0;

	char* end = buf + len;
	char* ls = buf;
	char* le = strecbrk(ls, end, '\n');

	if(le >= end)
		return 0;

	*le = '\0';

	if(strncmp(ls, "Name:\t", 6))
		return 0;

	char* name = ls + 6;
	int nlen = le - name;

	int i, n = ctx->ncmd;
	char** cmds = ctx->cmds;

	for(i = 0; i < n; i++) {
		char* cmd = cmds[i];

		if(strlen(cmd) >= 15)
			continue;
		if(slashes(cmd))
			continue;
		if(!strncmp(name, cmd, nlen))
			return 1;
	}

	return 0;
}

static int match_proc_cmd(CTX, int at, char* pstr)
{
	char buf[128];
	int len;

	if((len = read_proc_file(buf, sizeof(buf), at, pstr, "cmdline")) < 0)
		return 0;

	char* end = buf + len;
	char* cs = buf;
	char* ce = strecbrk(cs, end, '\0');

	if(ce >= end)
		return 0;

	char* path = cs;
	char* base = basename(path);

	int i, n = ctx->ncmd;
	char** cmds = ctx->cmds;

	for(i = 0; i < n; i++) {
		char* cmd = cmds[i];
		char* patt = slashes(cmd) ? path : base;

		if(!strcmp(cmd, patt))
			return 1;
	}

	return 0;
}

static void kill_proc(CTX, char* pstr)
{
	char* p;
	int pid, ret;

	if(!(p = parseint(pstr, &pid)) || *p) {
		warn("invalid pid", pstr, 0);
		return;
	}

	if((ret = sys_kill(pid, ctx->sig)) < 0) {
		warn(NULL, pstr, ret);
		ctx->failed++;
	} else {
		ctx->killed++;
	}
}

static void maybe_kill_proc(CTX, int at, char* pstr)
{
	if(match_proc_name(ctx, at, pstr))
		;
	else if(match_proc_cmd(ctx, at, pstr))
		;
	else return;

	kill_proc(ctx, pstr);
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

	FMTBUF(p, e, self, 50);
	p = fmtint(p, e, sys_getpid());
	FMTEND(p, e);

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
			if(!strcmp(de->name, self))
				continue;

			maybe_kill_proc(ctx, fd, de->name);
		}
	}
}

static void kill_selected_pids(CTX)
{
	int i, n = ctx->npid;
	char** pids = ctx->pids;

	for(i = 0; i < n; i++)
		kill_proc(ctx, pids[i]);
}

static const struct signal {
	unsigned char sig;
	char name[7];
} signals[] = {
	{ SIGHUP,  "HUP"  },
	{ SIGINT,  "INT"  },
	{ SIGQUIT, "QUIT" },
	{ SIGILL,  "ILL"  },
	{ SIGABRT, "ABRT" },
	{ SIGFPE,  "FPE"  },
	{ SIGKILL, "KILL" },
	{ SIGSEGV, "SEGV" },
	{ SIGPIPE, "PIPE" },
	{ SIGALRM, "ALRM" },
	{ SIGTERM, "TERM" },
	{ SIGUSR1, "USR1" },
	{ SIGUSR2, "USR2" },
	{ SIGCHLD, "CHLD" },
	{ SIGCONT, "CONT" },
	{ SIGSTOP, "STOP" },
	{ SIGBUS,  "BUS"  },
	{ SIGXCPU, "XCPU" },
	{ SIGXFSZ, "XFSZ" }
};

static int parse_signal(char* name)
{
	char* orig = name;
	const struct signal* sg;

	if(!strncmp(name, "SIG", 3))
		name += 3;

	for(sg = signals; sg < ARRAY_END(signals); sg++)
		if(!strncmp(name, sg->name, sizeof(sg->name)))
			return sg->sig;

	fail("unknown signal", orig, 0);
}

static int numeric(char* arg)
{
	char* p;

	for(p = arg; *p; p++)
		if(*p < '0' || *p > '9')
			return 0;
	
	return (p > arg);
}

static int choose_signal(int opts)
{
	if(!opts)
		return SIGTERM;
	if(opts == OPT_k)
		return SIGKILL;
	if(opts == OPT_h)
		return SIGHUP;
	if(opts == OPT_s)
		return SIGSTOP;
	if(opts == OPT_c)
		return SIGCONT;

	fail("invalid options", NULL, 0);
}

static void setup_args(CTX, int argc, char** argv)
{
	int i = 1, opts = 0, sig = 0;

	if(i < argc && argv[i][0] == '-') {
		char* rest = argv[i++] + 1;
		char lead = *rest;

		if(lead >= 'A' && lead <= 'Z')
			sig = parse_signal(rest);
		else
			opts = argbits(OPTS, rest);
	}

	if(i >= argc)
		fail("too few arguments", NULL, 0);
	if(!sig)
		sig = choose_signal(opts);

	ctx->sig = sig;
	ctx->opts = opts;

	while(i < argc) {
		char* arg = argv[i++];

		if(numeric(arg))
			ctx->pids[ctx->npid++] = arg;
		else
			ctx->cmds[ctx->ncmd++] = arg;
	}
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	char* pids[argc];
	char* cmds[argc];

	memzero(ctx, sizeof(*ctx));

	ctx->pids = pids;
	ctx->cmds = cmds;

	setup_args(ctx, argc, argv);

	if(ctx->npid)
		kill_selected_pids(ctx);
	if(ctx->ncmd)
		read_proc_list(ctx);

	if(ctx->failed)
		return 1;
	if(!ctx->killed && ctx->ncmd)
		fail("no matching processes", NULL, 0);

	return 0;
}
