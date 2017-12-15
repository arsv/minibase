#include <sys/mman.h>

#include <string.h>
#include <printf.h>
#include <format.h>
#include <output.h>
#include <util.h>

#include "cmd.h"

/* Environment, if changed, is kept in its own mmaped block. The way
   the parser part works around ; handling make it difficult to store
   it on the heap like msh does. Maybe at some point this should be change.

   (But this is the interactive shell so an extra page alloc caused
    by unlikely command probably doesn't matter much.)

   The layout of the environment block looks like this:

       Es Es Es ... Es ENVP
       ^buf         sep^   ^ptr

   Es = struct env (with inline data) and ENVP is char* envp[] with pointers
   to individual strings. Unlike in msh, there are no indirect pointers to
   the original strings, everything gets copied inline Es-es. */

struct env {
	int len;
	char str[];
};

static void* evalloc(struct environ* ev, int len)
{
	int old = ev->ptr;
	int ptr = old + len;
	int size = ev->size;

	if(ptr > size) {
		int newsize = size + pagealign(size - ptr);
		int flags = MREMAP_MAYMOVE;
		void* buf = ev->buf;
		void* new = sys_mremap(buf, size, newsize, flags);

		if(mmap_error(new)) {
			warn("mremap", NULL, (long)new);
			return NULL;
		}

		ev->buf = new;
		ev->size = newsize;
	};

	ev->ptr = ptr;

	return ev->buf + old;
}

static int mmapbuf(struct environ* ev)
{
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int size = PAGE;

	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if(mmap_error(buf)) {
		warn("mmap", NULL, (long)buf);
		return (long)buf;
	}

	ev->buf = buf;
	ev->size = size;
	ev->ptr = 0;
	ev->count = 0;

	return 0;
}

static void copyenv(CTX, struct environ* ev)
{
	struct env* ep;
	char** envp = ctx->envp;
	char** p;

	for(p = envp; *p; p++) {
		char* envi = *p;
		long len = strlen(envi);
		long total = sizeof(*ep) + len + 1;

		if(total > 0x7FFFFFFF)
			continue;
		if(!(ep = evalloc(ev, total)))
			continue;

		ep->len = total;
		memcpy(ep->str, envi, len + 1);

		ev->count++;
	}
}

static int initenv(CTX, struct environ* ev)
{
	int ret;

	if(!ev->buf) {
		if((ret = mmapbuf(ev)) < 0)
			return ret;

		copyenv(ctx, ev);

		ev->orig = ctx->envp;
	} else {
		ev->ptr = ev->sep;
	}

	ctx->envp = NULL;

	return 0;
}

static void unset(struct environ* ev, char* name)
{
	void* buf = ev->buf;
	int ptr = 0;
	int end = ev->ptr;
	int len;

	int nlen = strlen(name);

	while(ptr < end) {
		struct env* ep = (struct env*)(buf + ptr);

		if(!(len = ep->len))
			break;

		ptr += (len < 0 ? -len : len);

		if(len < 0)
			continue; /* deleted value */

		if(strncmp(ep->str, name, nlen))
			continue; /* wrong variable */
		if(ep->str[nlen] != '=')
			continue; /* wrong variable */

		ep->len = -len;

		ev->count--;
	}
}

static void append(struct environ* ev, char* name, char* val)
{
	int nlen = strlen(name);
	int vlen = strlen(val);
	struct env* ep;

	int total = sizeof(*ep) + nlen + 1 + vlen + 1;

	if(!(ep = evalloc(ev, total)))
		return;

	char* p = ep->str;

	memcpy(p, name, nlen); p += nlen;
	*p++ = '=';
	memcpy(p, val, vlen); p += vlen;
	*p = '\0';

	ep->len = total;

	ev->count++;
}

static int cmpvar(const void* a, const void* b)
{
	char** sa = (char**) a;
	char** sb = (char**) b;

	return strcmp(*sa, *sb);
}

static void rebuild(CTX, struct environ* ev)
{
	int ptr = 0;
	int end = ev->ptr;
	int len;
	int i = 0, count = ev->count;
	char** envp;

	ev->sep = ev->ptr;

	if(!(envp = evalloc(ev, (count+1)*sizeof(char*)))) {
		ctx->envp = ev->orig;
		return;
	}

	char* buf = ev->buf;

	while(ptr < end) {
		struct env* ep = (struct env*)(buf + ptr);

		if(!(len = ep->len))
			break;

		ptr += (len < 0 ? -len : len);

		if(len < 0)
			continue;
		if(i >= count)
			break;

		envp[i++] = ep->str;
	}

	envp[i++] = NULL;

	qsort(envp, count, sizeof(char*), cmpvar);

	ctx->envp = envp;
}

void envp_set(CTX, char* name, char* value)
{
	struct environ* ev = &ctx->env;

	initenv(ctx, ev);
	unset(ev, name);
	append(ev, name, value);
	rebuild(ctx, ev);
}

void envp_unset(CTX, char* name)
{
	struct environ* ev = &ctx->env;

	if(!getenv(ctx->envp, name))
		return warn("undefined variable", name, 0);

	initenv(ctx, ev);
	unset(ev, name);
	rebuild(ctx, ev);
}

void envp_dump(CTX, char* name)
{
	char* val;
	char** envp = ctx->envp;

	if(!(val = getenv(envp, name)))
		return warn("undefined variable", name, 0);

	int len = strlen(val);
	char out[len+1];

	memcpy(out, val, len);
	out[len] = '\n';

	writeall(STDOUT, out, len+1);
}

void envp_dump_all(CTX)
{
	char buf[2048];
	struct bufout bo = {
		.fd = STDOUT,
		.buf = buf,
		.ptr = 0,
		.len = sizeof(buf)
	};
	char** envp = ctx->envp;
	char** p;

	for(p = envp; *p; p++) {
		bufout(&bo, *p, strlen(*p));
		bufout(&bo, "\n", 1);
	}

	bufoutflush(&bo);
}
