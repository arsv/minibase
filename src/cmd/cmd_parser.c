#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>

#include "cmd.h"

struct arg {
	ushort len;
	char zstr[];
};

static void collect(CTX, int argc, char* hptr)
{
	void* ptr = hptr;
	void* end = ctx->hptr;

	if(!argc) return;

	char* argv[argc+1];
	int i;

	for(i = 0; i < argc; i++) {
		if(ptr + sizeof(struct arg) > end)
			goto err;

		struct arg* argp = ptr;
		ushort len = argp->len;
		
		if(len < 2)
			goto err;
		if(ptr + len > end)
			goto err;

		argv[i] = argp->zstr;
		ptr += len;
	}
	
	if(ptr < end)
		goto err;

	argv[i] = NULL;

	return execute(ctx, argc, argv);
err:
	warn("parser error", NULL, 0);
}

static int extend(CTX, int len)
{
	if(ctx->hptr + len < ctx->hend)
		return 0;
	if(len >= PAGE) {
		warn("argument too long", NULL, 0);
		return -1;
	}

	char* brk = ctx->hend;
	char* end = sys_brk(brk + PAGE);

	if(end <= brk) {
		warn("cannot allocate memory", NULL, 0);
		return -1;
	}

	ctx->hend = end;

	return 0;
}

static int addchar(CTX, char c)
{
	int ret;

	if((ret = extend(ctx, 1)))
		return ret;

	*(ctx->hptr++) = c;

	return 0;
}

static int append(CTX, void* buf, int len)
{
	int ret;

	if((ret = extend(ctx, 1)))
		return ret;

	char* dst = ctx->hptr;
	memcpy(dst, buf, len);
	ctx->hptr = dst + len;

	return 0;
}

static void* startarg(CTX)
{
	struct arg empty = { .len = 0 };
	char* ptr = ctx->hptr;

	if(append(ctx, &empty, sizeof(empty)))
		return NULL;

	return ptr;
}

static int endarg(CTX, void* ptr)
{
	int ret;
	struct arg* argp = ptr;

	if((ret = addchar(ctx, '\0')))
		return ret;

	void* end = ctx->hptr;
	long len = end - ptr;

	if(len & ~0xFFFF) {
		warn("argument too long", NULL, 0);
		return -1;
	}

	argp->len = len & 0xFFFF;

	return 0;
}

static char* variable(CTX, char* p, char* e)
{
	tracef("variable\n");
	return NULL;
}

static char* dquote(CTX, char* p, char* e)
{
	tracef("dquote\n");
	return NULL;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t' || c == '\n' || !c);
}

static int isargsep(int c)
{
	return (c == ';');
}

static char* argument(CTX, char* p, char* e)
{
	while(p < e) {
		char c = *p++;

		if(isargsep(c))
			return p - 1;
		if(isspace(c) || !c)
			break;
		else if(c == '$')
			p = variable(ctx, p, e);
		else if(c == '"')
			p = dquote(ctx, p, e);
		else if(addchar(ctx, c))
			return NULL;

		if(!p) return p;
	}

	return p;
}

static char* skipspace(char* p, char* e)
{
	for(; p < e; p++)
		if(!isspace(*p))
			break;

	return p;
}

void parse(CTX, char* buf, int len)
{
	char* base = ctx->hptr;

	char* p = buf;
	char* e = buf + len;
	int argc = 0;

	while(1) {
		if((p = skipspace(p, e)) >= e)
			break;

		if(*p == ';') {
			collect(ctx, argc, base);
			p++;
			argc = 0;
			ctx->hptr = base;
		} else {
			void* argp;

			if(!(argp = startarg(ctx)))
				return;
			if(!(p = argument(ctx, p, e)))
				return;
			if(endarg(ctx, argp))
				return;

			argc++;
		}
	}

	collect(ctx, argc, base);
	ctx->hptr = base;
}
