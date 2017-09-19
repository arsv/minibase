#include <sys/info.h>
#include <sys/mman.h>
#include <sys/module.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <dirs.h>

#include "modprobe.h"

void prep_release(CTX)
{
	struct utsname uts;
	int ret;

	if(ctx->release)
		return;

	if((ret = sys_uname(&uts)) < 0)
		fail("uname", NULL, ret);

	ctx->release = heap_dup(ctx, uts.release);

	ctx->lwm = ctx->ptr;
}

void prep_modules_dep(CTX)
{
	if(ctx->modules_dep.buf)
		return;

	prep_release(ctx);

	char* release = ctx->release;

	FMTBUF(p, e, path, strlen(release) + 40);
	p = fmtstr(p, e, "/lib/modules/");
	p = fmtstr(p, e, release);
	p = fmtstr(p, e, "/modules.dep");
	FMTEND(p, e);

	mmap_whole(&ctx->modules_dep, path, 1);
}

void prep_etc_modopts(CTX)
{
	if(ctx->did_modopts)
		return;

	ctx->did_modopts = 1;

	mmap_whole(&ctx->etc_modopts, ETCDIR "/modopts", 0);
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* skip_space(char* p, char* e)
{
	while(p < e && isspace(*p))
		p++;
	return p;
}

static char* skip_word(char* p, char* e)
{
	while(p < e && !isspace(*p))
		p++;
	return p;
}

static char* match_dep(char* ls, char* le, char* name, int nlen)
{
	char* p = strecbrk(ls, le, ':');

	if(p >= le)
		return NULL;

	char* q = p - 1;

	while(q > ls && *q != '/') q--;

	if(*q == '/') q++;

	if(le - q < nlen)
		return NULL;

	if(strncmp(q, name, nlen))
		return NULL;
	if(q[nlen] != '.')
		return NULL;

	return p;
}

static void add_dep(CTX, char* p, char* e)
{
	(void)heap_dupe(ctx, p, e);
}

int locate_line(struct mbuf* mb, struct line* ln, lnmatch lnm, char* name)
{
	int nlen = strlen(name);

	char* bs = mb->buf;
	char* be = bs + mb->len;
	char *ls, *le;
	char *p;

	for(ls = bs; ls < be; ls = le + 1) {
		le = strecbrk(ls, be, '\n');

		if(!(p = lnm(ls, le, name, nlen)))
			continue;

		ln->ptr = ls;
		ln->sep = p;
		ln->end = le;

		return 0;
	}

	return -ENOENT;
}

static char** index_deps(CTX, char* ptr, char* end)
{
	int cnt = 1;
	char* p;

	for(p = ptr; p < end - 1; p++)
		if(!*p) cnt++;

	char** idx = heap_alloc(ctx, (cnt+1)*sizeof(char*));
	int i = 0;

	idx[i++] = ptr;

	for(p = ptr; p < end - 1; p++)
		if(!*p) idx[i++] = p + 1;

	idx[i] = NULL;

	return idx;
}

static char** pack_deps(CTX, struct line* ln)
{
	char* ls = ln->ptr;
	char* le = ln->end;
	char *p, *q;

	p = ln->sep;

	char* ptr = ctx->ptr;

	add_dep(ctx, ls, p++);

	while(p < le) {
		p = skip_space(p, le);
		q = skip_word(p, le);

		add_dep(ctx, p, q);

		p = q + 1;
	}

	char* end = ctx->ptr;

	return index_deps(ctx, ptr, end);
}

char** query_deps(CTX, char* name)
{
	struct mbuf* mb = &ctx->modules_dep;
	struct line ln;

	prep_modules_dep(ctx);

	if(locate_line(mb, &ln, match_dep, name))
		return NULL;

	return pack_deps(ctx, &ln);
}

static char* match_opt(char* ls, char* le, char* name, int nlen)
{
	char* p = strecbrk(ls, le, ':');

	if(p >= le || p - ls < nlen)
		return NULL;
	if(strncmp(ls, name, nlen))
		return NULL;
	if(p != ls + nlen)
		return NULL;

	return p;
}

char* query_pars(CTX, char* name)
{
	struct mbuf* mb = &ctx->etc_modopts;
	struct line ln;

	prep_etc_modopts(ctx);

	if(!mb->buf)
		return NULL;
	if(locate_line(mb, &ln, match_opt, name))
		return NULL;

	char* p = skip_space(ln.sep + 1, ln.end);

	return heap_dupe(ctx, p, ln.end);
}
