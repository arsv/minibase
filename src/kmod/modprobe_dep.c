#include <sys/info.h>
#include <sys/mman.h>
#include <sys/module.h>

#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>
#include <dirs.h>

#include "common.h"
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

static void prep_modules_file(CTX, struct mbuf* mb, char* name, int mode)
{
	if(mb->tried) return;

	prep_release(ctx);

	char* release = ctx->release;

	FMTBUF(p, e, path, strlen(release) + 40);
	p = fmtstr(p, e, "/lib/modules/");
	p = fmtstr(p, e, release);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	mmap_whole(mb, path, mode);
}

void prep_modules_dep(CTX)
{
	prep_modules_file(ctx, &ctx->modules_dep, "modules.dep", STRICT);
}

void prep_modules_alias(CTX)
{
	prep_modules_file(ctx, &ctx->modules_alias, "modules.alias", FAILOK);
}

void prep_config(CTX)
{
	mmap_whole(&ctx->config, CONFIG, FAILOK);
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

/* Sometimes a module named foo-bar resides in a file named foo_bar.ko
   and vice versa. There are no apparent rules for this, so we just
   collate - with _ and match the names that way. */

static char eq(char c)
{
	return (c == '_' ? '-' : c);
}

static int xstrncmp(char* a, char* b, int len)
{
	char* e = b + len;

	while(*a && b < e && *b)
		if(eq(*a++) != eq(*b++))
			return -1;

	if(b >= e)
		return 0;
	if(*a == *b)
		return 0;

	return -1;
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

	if(xstrncmp(q, name, nlen))
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

	if(!mb->buf)
		goto out;

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
out:
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

static char* word(char* p, char* e, char* word)
{
	int len = strlen(word);

	if(e - p < len)
		return NULL;
	if(strncmp(p, word, len))
		return NULL;
	if(p + len >= e)
		return e;
	if(!isspace(p[len]))
		return NULL;

	return skip_space(p + len, e);
}

static char* match_opt(char* ls, char* le, char* name, int nlen)
{
	(void)nlen;
	char* p = ls;
	char* e = le;

	if(!(p = word(p, e, "options")))
		return NULL;
	if(!(p = word(p, e, name)))
		return NULL;

	return p;
}

static char* match_blacklist(char* ls, char* le, char* name, int nlen)
{
	(void)nlen;
	char* p = ls;
	char* e = le;

	if(!(p = word(p, e, "blacklist")))
		return NULL;
	if(!(p = word(p, e, name)))
		return NULL;

	return p;
}

char* query_pars(CTX, char* name)
{
	struct mbuf* mb = &ctx->config;
	struct line ln;

	prep_config(ctx);

	if(!mb->buf)
		return NULL;
	if(locate_line(mb, &ln, match_opt, name))
		return NULL;

	char* p = skip_space(ln.sep, ln.end);

	return heap_dupe(ctx, p, ln.end);
}

/* Note wildcard handling here is wrong, but it's enough to get kernel
   aliases working.

   pci:v000010ECd0000525Asv00001028sd000006DEbcFFsc00i00
   pci:v000010ECd0000525Asv*       sd*       bc* sc* i*

   The values being matched with * are uppercase hex while the terminals
   are lowercase letters, so we can just skip until the next non-* character
   from the pattern. */

static char* match_alias(char* ls, char* le, char* name, int nlen)
{
	(void)nlen;
	char* p = ls;
	char* e = le;
	char* n = name;

	if(!(p = word(p, e, "alias")))
		return NULL;

	while(*n && p < e) {
		if(isspace(*p)) {
			break;
		} else if(*p == '*') {
			p++;
			while(*n && *n != *p)
				n++;
		} else if(*p != *n) {
			return NULL;
		} else {
			p++;
			n++;
		};
	} if(*n || p >= e || !isspace(*p)) {
		return NULL;
	}

	return skip_space(p, e);
}

char* query_alias(CTX, char* name)
{
	struct mbuf* ma = &ctx->modules_alias;
	struct mbuf* cf = &ctx->config;
	struct line ln;

	prep_config(ctx);
	prep_modules_alias(ctx);

	if(locate_line(cf, &ln, match_alias, name) >= 0)
		goto got;
	if(locate_line(ma, &ln, match_alias, name) >= 0)
		goto got;

	return NULL;
got:
	return heap_dupe(ctx, ln.sep, ln.end);
}

int is_blacklisted(CTX, char* name)
{
	struct mbuf* mb = &ctx->config;
	struct line ln;

	prep_config(ctx);

	if(locate_line(mb, &ln, match_blacklist, name))
		return 0;

	return 1;
}
