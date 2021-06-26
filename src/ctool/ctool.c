#include <sys/mman.h>
#include <sys/file.h>

#include <string.h>
#include <format.h>
#include <main.h>
#include <util.h>

#include "ctool.h"

ERRTAG("ctool");

static void* heap_alloc(CTX, int size)
{
	void* brk = ctx->brk;
	void* ptr = ctx->ptr;
	void* end = ctx->end;

	if(!brk) {
		brk = sys_brk(NULL);
		ptr = brk;
		end = brk;
		ctx->brk = ptr;
		ctx->end = end;
	}

	size = (size + 3) & ~3;

	void* new = ptr + size;

	if(new > end) {
		uint need = pagealign(new - end);
		void* tmp = sys_brk(end + need);

		if(brk_error(end, tmp))
			fail("brk", NULL, -ENOMEM);

		ctx->end = end = tmp;
	}

	ctx->ptr = new;

	return ptr;
}

void* alloc_exact(CTX, int size)
{
	if(size % 4)
		fail("non-aligned exact alloc", NULL, 0);

	return heap_alloc(ctx, size);
}

void* alloc_align(CTX, int size)
{
	size = (size + 3) & ~3;

	return heap_alloc(ctx, size);
}

void heap_reset(CTX, void* ptr)
{
	if(!ptr || (ptr < ctx->brk) || (ptr > ctx->ptr))
		fail(NULL, NULL, -EFAULT);

	ctx->ptr = ptr;
}

void failz(CTX, const char* msg, char* name, int err)
{
	int i, n = ctx->depth;
	int len = 16;

	for(i = 0; i < n; i++)
		len += strlen(ctx->path[i]) + 1;

	if(name) len += strlen(name);

	char* buf = alloca(len);
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, "/");

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, ctx->path[i]);
		p = fmtstr(p, e, "/");
	}

	if(name) p = fmtstr(p, e, name);

	*p++ = '\0';

	fail(msg, buf, err);
}

static void warn_pref_relative(CTX, const char* msg, char* name, int err)
{
	char* root = NULL;
	int i, n = ctx->depth;
	int len = 16;

	if(root) len += strlen(root) + 1;

	for(i = 0; i < n; i++)
		len += strlen(ctx->path[i]) + 1;

	if(name) len += strlen(name);

	char* buf = alloca(len);
	char* p = buf;
	char* e = buf + len - 1;

	if(root) {
		p = fmtstr(p, e, root);
		p = fmtstr(p, e, "/");
	}

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, ctx->path[i]);
		p = fmtstr(p, e, "/");
	}

	if(name) p = fmtstr(p, e, name);

	*p++ = '\0';

	warn(msg, buf, err);
}

void warnx(CTX, const char* msg, char* name, int err)
{
	warn_pref_relative(ctx, msg, name, err);

	ctx->fail = 1;
}

void failx(CTX, const char* msg, char* name, int err)
{
	warn_pref_relative(ctx, msg, name, err);

	_exit(0xFF);
}

void need_zero_depth(CTX)
{
	if(ctx->depth)
		fail("non-zero inital depth", NULL, 0);
}

char* shift(CTX)
{
	int i = ctx->argi;

	if(i >= ctx->argc)
		fail("too few arguments", NULL, 0);

	ctx->argi = i + 1;

	return ctx->argv[i];
}

int args_left(CTX)
{
	return (ctx->argc - ctx->argi);
}

void no_more_arguments(CTX)
{
	if(ctx->argi < ctx->argc)
		fail("too many arguments", NULL, 0);
}

static void dispatch_word(CTX, char* cmd)
{
	if(!strcmp(cmd, "use"))
		return cmd_use(ctx);
	if(!strcmp(cmd, "repo"))
		return cmd_repo(ctx);
	if(!strcmp(cmd, "add"))
		return cmd_add(ctx);
	if(!strcmp(cmd, "del"))
		return cmd_del(ctx);
	if(!strcmp(cmd, "rebin"))
		return cmd_rebin(ctx);
	if(!strcmp(cmd, "reset"))
		return cmd_reset(ctx);

	fail("unknown command", cmd, 0);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->argi = 1;
	ctx->envp = argv + argc + 1;

	ctx->at = AT_FDCWD;
	ctx->pacfd = -1;
	ctx->fdbfd = -1;

	if(argc < 2)
		fail("too few arguments", NULL, 0);

	char* cmd = shift(ctx);

	dispatch_word(ctx, cmd);

	return 0;
}
