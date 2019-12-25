#include <sys/file.h>
#include <sys/proc.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "msh.h"
#include "msh_cmd.h"

/* Common routines used by most commands (and by some other parts
   of the code as well).

   Library quit() and warn() are not very well suited for msh,
   which should preferably use script name and line much more
   often than generic "msh:" tag. */

static void report(CTX, const char* err, char* arg, long ret)
{
	FMTBUF(p, e, buf, 512);

	if(ctx->file) {
		p = fmtstr(p, e, ctx->file);
		p = fmtstr(p, e, ":");
		p = fmtint(p, e, ctx->line);
		p = fmtstr(p, e, ":");
	} else {
		p = fmtstr(p, e, "msh:");
	}
	if(err) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, err);
	} if(arg) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, arg);
	} if((arg || err) && ret) {
		p = fmtstr(p, e, ": ");
	} if(ret) {
		p = fmterr(p, e, ret);
	}

	FMTENL(p, e);

	writeall(ctx->errfd, buf, p - buf);
}

/* Variable access routines */

struct env* env_first(CTX)
{
	void* p = ctx->heap;
	void* e = ctx->asep;

	struct env* ev = p;

	if(p + sizeof(*ev) > e)
		return NULL;

	int key = ev->key;
	int size;

	if(key & EV_REF)
		size = sizeof(*ev);
	else
		size = key & EV_SIZE;

	if(size < sizeof(*ev))
		return NULL;
	if(p + size > e)
		return NULL;

	return ev;
}

struct env* env_next(CTX, struct env* ev)
{
	void* s = ctx->heap;
	void* p = ev;
	void* e = ctx->asep;

	if(p < s || p >= e)
		return NULL;

	int key = ev->key;

	if(key & EV_REF)
		p += sizeof(*ev);
	else
		p += (key & EV_SIZE);

	ev = p;

	if(p + sizeof(*ev) > e)
		return NULL;

	if(key & EV_REF)
		p += sizeof(*ev);
	else
		p += (key & EV_SIZE);

	if(p > e)
		return NULL;

	return ev;
}

char* env_value(CTX, struct env* ev, int type)
{
	int key = ev->key;

	if(!(key & type))
		return NULL;
	if(!(key & EV_REF))
		return ev->payload;

	int idx = key & EV_SIZE;

	return ctx->environ[idx];
}

/* In case `onexit` has been used, invoke it when exiting. */

static void try_exec_trap(CTX)
{
	struct env* ev;
	int i, count = 0;
	char* argv[32];
	char* arg;

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev))
		if((arg = env_value(ctx, ev, EV_TRAP)))
			count++;

	if(!count)
		return;
	if(count >= ARRAY_SIZE(argv))
		count = ARRAY_SIZE(argv) - 1;

	for(ev = env_first(ctx); ev; ev = env_next(ctx, ev))
		if((arg = env_value(ctx, ev, EV_TRAP)))
			if(i < count) argv[i++] = arg;

	argv[i] = NULL;

	int ret = sys_execve(*argv, argv, ctx->environ);

	report(ctx, "exec", *argv, ret);
}

void exit(CTX, int code)
{
	try_exec_trap(ctx);

	_exit(code);
}

/* For a few cases where error is related to msh dealing with the script
   file itself, "msh: error message" reports are better. */

void quit(CTX, const char* err, char* arg, int ret)
{
	ctx->file = NULL; /* force msh: tag */
	report(ctx, err, arg, ret);
	exit(ctx, 0xFF);
}

/* These two do the same exact thing, error() takes errno and fatal() does not.
   Originally there was a distinction that did warrant fatal() to be named like
   that (error was recoverable and fatal was not), but the whole thing has been
   reworked since and fatal() has been kept as just a handy shortcut. */

void error(CTX, const char* err, char* arg, int ret)
{
	report(ctx, err, arg, ret);
	exit(ctx, 0xFF);
}

void fatal(CTX, const char* err, char* arg)
{
	report(ctx, err, arg, 0);
	exit(ctx, 0xFF);
}

/* Arguments handling for builtins */

void check(CTX, const char* msg, char* arg, int ret)
{
	if(ret < 0) error(ctx, msg, arg, ret);
}

int got_more_arguments(CTX)
{
	return (ctx->argp < ctx->argc);
}

void no_more_arguments(CTX)
{
	if(ctx->argp < ctx->argc)
		fatal(ctx, "too many arguments", NULL);
}

void need_some_arguments(CTX)
{
	if(ctx->argp >= ctx->argc)
		fatal(ctx, "argument required", NULL);
}

char** argsleft(CTX)
{
	return &(ctx->argv[ctx->argp]);
}

char* next(CTX)
{
	if(ctx->argp >= ctx->argc)
		return NULL;

	return ctx->argv[ctx->argp++];
}

char* shift(CTX)
{
	if(ctx->argp >= ctx->argc)
		fatal(ctx, "too few arguments", NULL);

	return ctx->argv[ctx->argp++];
}

char* dash_opts(CTX)
{
	if(ctx->argp >= ctx->argc)
		return NULL;

	char* lead = ctx->argv[ctx->argp];

	if(*lead != '-')
		return NULL;

	ctx->argp++;

	return lead + 1;
}

void shift_int(CTX, int* dst)
{
	char* str = shift(ctx);
	char* p;

	if(!(p = parseint(str, dst)) || *p)
		fatal(ctx, "invalid number:", str);
}

void shift_u64(CTX, uint64_t* dst)
{
	char* str = shift(ctx);
	char* p;

	if(!(p = parseu64(str, dst)) || *p)
		fatal(ctx, "invalid number:", str);
}

void shift_oct(CTX, int* dst)
{
	char* str = shift(ctx);
	char* p;

	if(*str != '0')
		fatal(ctx, "invalid octal:", str);
	if(!(p = parseoct(str, dst)) || *p)
		fatal(ctx, "invalid octal:", str);
}

/* Heap routines. See msh.h for heap layout. */

void heap_init(CTX)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + 4096);

	if(brk_error(brk, end))
		quit(ctx, "heap init failed", NULL, 0);

	ctx->heap = brk;
	ctx->asep = brk;
	ctx->hptr = brk;
	ctx->hend = end;
}

void heap_extend(CTX)
{
	void* end = ctx->hend;
	void* new = sys_brk(end + PAGE);

	if(brk_error(end, new))
		fatal(ctx, "cannot allocate memory", NULL);

	ctx->hend = new;
}

void* heap_alloc(CTX, int len)
{
	void* ptr = ctx->hptr;
	void* end = ctx->hend;
	void* newptr = ptr + len;

	if(newptr <= end)
		goto ptr;

	int need = pagealign(newptr - end);
	void* newend = sys_brk(end + need);

	if(brk_error(end, newend))
		fatal(ctx, "cannot allocate memory", NULL);

	ctx->hend = newend;
ptr:
	ctx->hptr = newptr;

	return ptr;
}

/* Loading /etc/passwd and seccomp files whole. We never unmmap them,
   r/o private mappings should not cause issues and in most cases the
   script in question should exec into something else anyway. */

void map_file(CTX, struct mbuf* mb, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY | O_CLOEXEC)) < 0)
		error(ctx, NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		error(ctx, "stat", name, ret);
	if(st.size > 0x7FFFFFFF) /* no larger-than-int files */
		error(ctx, NULL, name, -E2BIG);

	int proto = PROT_READ;
	int flags = MAP_PRIVATE;

	void* ptr = sys_mmap(NULL, st.size, proto, flags, fd, 0);

	if((ret = mmap_error(ptr)))
		error(ctx, "mmap", name, ret);

	mb->len = st.size;
	mb->buf = ptr;

	sys_close(fd);
}
