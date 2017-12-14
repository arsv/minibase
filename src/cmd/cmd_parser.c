#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <printf.h>
#include <util.h>

#include "cmd.h"

/* Primary command parser. Takes complete command line, turns it
   into argc, argv[] and calls execute().

   The parts for argv[] are assembled in the heap, right after
   the original string. Quotes etc prevent us from using the source
   string directly, and variable substitution happens here as well. */

struct arg {
	ushort len;
	char zstr[];
};

static void collect(CTX, int argc, char* base)
{
	struct heap* hp = &ctx->heap;
	void* ptr = base;
	void* end = hp->ptr;

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

static int addchar(CTX, char c)
{
	char* dst;

	if(!(dst = alloc(ctx, 1)))
		return -1;

	*dst = c;

	return 0;
}

static int append(CTX, void* buf, int len)
{
	char* dst;

	if(!(dst = alloc(ctx, len)))
		return -1;

	memcpy(dst, buf, len);

	return 0;
}

static void* startarg(CTX)
{
	struct arg* dst;

	if(!(dst = alloc(ctx, sizeof(*dst))))
		return NULL;

	dst->len = 0;

	return dst;
}

static int endarg(CTX, void* ptr)
{
	int ret;
	struct arg* argp = ptr;

	if((ret = addchar(ctx, '\0')))
		return ret;

	struct heap* hp = &ctx->heap;
	void* end = hp->ptr;
	long len = end - ptr;

	if(len & ~0xFFFF) {
		warn("argument too long", NULL, 0);
		return -1;
	}

	argp->len = len & 0xFFFF;

	return 0;
}

static char* backslash(CTX, char* p, char* e)
{
	if(p >= e) {
		warn("trailing backslash", NULL, 0);
		return NULL;
	}

	int c = *p++;

	if(c == 'n')
		addchar(ctx, '\n');
	else if(c == 't')
		addchar(ctx, '\t');
	else
		addchar(ctx, c);

	return p;
}

static char* longvar(CTX, char* p, char* e)
{
	char* s = p;

	for(; p < e; p++) {
		if(*p >= 'a' && *p <= 'z')
			continue;
		if(*p >= 'A' && *p <= 'Z')
			continue;
		if(*p >= '0' && *p <= '9')
			continue;
		if(*p == '_')
			continue;
		break;
	}

	long len = p - s;
	char name[len+1];
	memcpy(name, s, len);
	name[len] = '\0';

	char* value = getenv(ctx->envp, name);

	if(!value) {
		warn("undefined variable", name, 0);
		return NULL;
	}

	int vlen = strlen(value);

	append(ctx, value, vlen);

	return p;
}

static char* shortvar(CTX, char* p, char* e, int c)
{
	char name[] = { '$', c, '\0' };
	warn("undefined variable", name, 0);
	return NULL;
}

static char* variable(CTX, char* p, char* e)
{
	if(p >= e) {
		warn("trailing $", NULL, 0);
		return NULL;
	}

	int c = *p;

	if(c <= 0x20 || c >= 0x7F) {
		warn("illegal variable name", NULL, 0);
		return NULL;
	};

	if(c >= 'a' && c <= 'z')
		return longvar(ctx, p, e);
	if(c >= 'A' && c <= 'Z')
		return longvar(ctx, p, e);

	return shortvar(ctx, p, e, c);
}

static char* squote(CTX, char* p, char* e)
{
	while(p < e) {
		int c = *p++;

		if(c == '\'')
			return p;
		else if(c == '\\')
			p = backslash(ctx, p, e);
		else
			addchar(ctx, c);

		if(!p) return p;
	}

	warn("unclosed double quotes", NULL, 0);
	return NULL;
}

static char* dquote(CTX, char* p, char* e)
{
	while(p < e) {
		int c = *p++;

		if(c == '"')
			return p;
		else if(c == '\\')
			p = backslash(ctx, p, e);
		else if(c == '$')
			p = variable(ctx, p, e);
		else
			addchar(ctx, c);

		if(!p) return p;
	}

	warn("unclosed double quotes", NULL, 0);
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
		else if(c == '\\')
			p = backslash(ctx, p, e);
		else if(c == '$')
			p = variable(ctx, p, e);
		else if(c == '\'')
			p = squote(ctx, p, e);
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
	struct heap* hp = &ctx->heap;
	char* base = hp->ptr;

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
			hp->ptr = base;
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
	hp->ptr = base;
}
