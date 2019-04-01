#include <sys/file.h>
#include <sys/proc.h>
#include <sys/fprop.h>

#include <string.h>
#include <main.h>
#include <util.h>

#define OPTS "01a"
#define OPT_0 (1<<0)	/* 0-terminated */
#define OPT_1 (1<<1)	/* one argument per command */
#define OPT_a (1<<2)	/* read arguments from a file */

ERRTAG("xargs");
ERRLIST(NEAGAIN NENOMEM NENOSYS NE2BIG NEACCES NEFAULT NEIO NEISDIR
	NELIBBAD NELOOP NENFILE NEMFILE NENOEXEC NENOTDIR NEPERM NETXTBSY
	NEFBIG NEINTR NEINVAL NENOENT NEBADF);

#define MAXARGS 64	/* arguments to pass to a single spawned command */

char inbuf[8*4096];	/* sizeof(inbuf)/2 is also the upper limit
			   on a single argument length */
struct exec {
	int total;	/* slots in argv (w/o terminating NULL) */
	int start;	/* index of the first non-constant slot */
	int ptr;	/* index of the first free slot */
	char* exe;
	char** argv;
	char** envp;
	int opts;
};

static int xopen(const char* fname)
{
	int fd;

	if((fd = sys_open(fname, O_RDONLY)) < 0)
		fail(NULL, fname, fd);

	return fd;
}

static void spawn(struct exec* ctx)
{
	int pid, ret, status;

	if((pid = sys_fork()) < 0) {
		fail("fork", NULL, pid);
	} else if(pid == 0) {
		ret = sys_execve(ctx->exe, ctx->argv, ctx->envp);
		fail("exec", ctx->exe, ret);
	} else {
		if((ret = sys_waitpid(pid, &status, 0)) < 0)
			fail("waitpid", NULL, ret);
		if(status)
			fail("command failed, aborting", NULL, 0);
	};
}

static char* endofline(char* ls, char* end, int opts)
{
	char* p;

	for(p = ls; p < end; p++)
		if(*p == '\n' && !(opts & OPT_0))
			return p;
		else if(*p == '\0')
			return p;

	return NULL;
}

/* xargs always spawns whatever it gets for a single useblock call in
   a single command. With regular reading logic, this minimizes
   "reaction time" but may result in more spawns than necessary.

   If large batches and lower number of spawns is preferable, the reading
   loop can be adjusted to only call useblock() once enough data has been
   accumulated. This is not implemented atm.

   For blocks that aren't last in the file, non-terminated lines
   (those not ending in \n) are assumed to be incomplete, and left
   untouched. Subsequent readinput will append more bytes to the
   block, possibly completing it, which the next call of useblock
   will check. */

#define REGULAR 0
#define LASTONE 1

static int useblock(struct exec* ctx, char* buf, int len, int lastone)
{
	char* end = buf + len;
	char* ls = buf;  /* line start */
	char* le;        /* line end */
	int needflush;
	int morelines;

	ctx->ptr = ctx->start; /* assumption: start < total */

	do {
		if((le = endofline(ls, end, ctx->opts)))
			*le = '\0';

		if(le || lastone)
			ctx->argv[ctx->ptr++] = ls;

		morelines = (le && (ls = le + 1) < end);

		if(morelines && ctx->ptr < ctx->total)
			needflush = 0;
		else
			needflush = (ctx->ptr > ctx->start);

		if(needflush) {
			ctx->argv[ctx->ptr] = NULL;

			spawn(ctx);

			ctx->ptr = ctx->start;
		}
	} while(morelines);

	return ls - buf;
}

/* Input stream is read blockwise, with inbuf acting as a moving window.
   Stepping is non-constant, and we move the data to keep the start of
   the next argument at the start of the buffer for each iteration.

   That's probably not necessary if there's enough space remaining in buf,
   but then if so, copying shouldn't take too much time anyway.

   In case input is a real file, mmaping it and calling useblock(LASTONE)
   on the mmaped buffer could be faster, but this case is so rare we
   do not bother. And even with mmaped file there would be a moving window,
   and lots of code to support it. */

static void readinput(struct exec* ctx, int fd)
{
	long rd;
	char* buf = inbuf;
	int len = sizeof(inbuf) - 1;  /* total usable length */
	int ptr = 0;  /* filled with data up to ptr */
	int stp = 0;  /* start processing next chunk from here */

	while((rd = sys_read(fd, buf + ptr, len - ptr)) > 0) {
		ptr += rd;

		int used = useblock(ctx, buf + stp, ptr - stp, REGULAR);

		if(used > len/2) {
			memcpy(buf, buf + used, ptr - used);
			stp = 0;
			ptr -= used;
		} else if(ptr >= len) {
			/* There's something longer than len/2 in buf
			   that useblock could not consume. */
			fail("argument too long", NULL, 0);
		} else {
			stp += used;
		}
	} if(stp < ptr) {
		useblock(ctx, buf + stp, ptr - stp, LASTONE);
	}
}

/* The following chunk sets up the command to run.
   We need a copy of argv with extra space added,
   and we need full path to the executable.

   The code is messy, so it's split into several functions.

   Command name is only resolved once instead of calling exec*p
   each time it needs to be executed. */

static int lookslikepath(const char* cmd)
{
	const char* p;

	for(p = cmd; *p; p++)
		if(*p == '/')
			return 1;

	return 0;
}

static void makefullname(char* buf, char* dir, int dirlen, char* cmd, int cmdlen)
{
	char* p = buf;
	/* buf[dirlen+cmdlen+2] <- "dir/cmd" */
	memcpy(p, dir, dirlen); p += dirlen; *p++ = '/';
	memcpy(p, cmd, cmdlen); p += cmdlen; *p = '\0';
}

static int isexecutable(char* dir, int dirlen, char* cmd, int cmdlen)
{
	char name[dirlen + cmdlen + 2];
	makefullname(name, dir, dirlen, cmd, cmdlen);
	return (sys_access(name, X_OK) >= 0);
}

char* whichexe(char* path, char* cmd)
{
	char *p, *q;
	int len = strlen(cmd);

	for(p = path; *(q = p); p = q + 1) {
		while(*q && *q != ':')
			q++;
		if(isexecutable(p, q - p, cmd, len))
			return p;
	}

	return NULL;
}

static void makecmd(struct exec* ctx, int fd, char** envp)
{
	char* cmd = *(ctx->argv);

	char* PATH = getenv(envp, "PATH"); if(!PATH)
		fail("cannot exec plain command with no PATH set", NULL, 0);

	char* dir = whichexe(PATH, cmd); if(!dir)
		fail("unknown command", cmd, 0);

	char* end = strcbrk(dir, ':');
	int dirlen = end - dir;
	int cmdlen = strlen(cmd);
	char cmdbuf[dirlen + cmdlen + 2];

	makefullname(cmdbuf, dir, dirlen, cmd, cmdlen);

	ctx->exe = cmdbuf;
	readinput(ctx, fd);
}

static void runpath(struct exec* ctx, int fd)
{
	ctx->exe = *(ctx->argv);
	readinput(ctx, fd);
}

static void makectx(int argc, char** argv, char** envp, int fd, int opts)
{
	int args = ((opts & OPT_1) ? 1 : MAXARGS);
	char* cmdv[argc + args + 1];
	struct exec ctx = {
		.start = argc,
		.total = argc + args,
		.ptr = 0,
		.argv = cmdv,
		.envp = envp,
		.opts = opts
	};
	memcpy(cmdv, argv, argc*sizeof(char*));

	if(lookslikepath(*cmdv))
		runpath(&ctx, fd);
	else
		makecmd(&ctx, fd, envp);
}

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;
	int i = 1, opts = 0;
	int fd = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc && (opts & OPT_a))
		fd = xopen(argv[i++]);
	else if(opts & OPT_a)
		fail("file name required", NULL, 0);

	if(i >= argc)
		fail("need a command to run", NULL, 0);

	makectx(argc - i, argv + i, envp, fd, opts);

	return 0;
}
