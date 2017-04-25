#include <sys/uname.h>

#include <format.h>
#include <string.h>
#include <fail.h>

#include "kmod.h"

#define MAX_RELEASE_LEN 65

struct modctx {
	char* name;     /* "e1000e" */
	char* dir;      /* "/lib/modules/4.8.13-1-ARCH/" */

	struct mbuf md; /* mmaped modules.dep */

	char* ls;       /* line for this particular module */
	char* le;
	char* sep;

	char* pars;
	char** envp;
};

static char* eol(char* p, char* end)
{
	while(p < end && *p != '\n')
		p++;
	return p;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static void concat(char* buf, int len, char* a, char* b)
{
	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, a);
	p = fmtstr(p, e, b);

	*p++ = '\0';
}

static char* match_depmod_line(char* ls, char* le, char* name, int nlen)
{
	if(*ls == '#')
		return NULL;
	if(*ls == ':')
		return NULL;

	char* sep = strecbrk(ls, le, ':');

	if(sep == le)
		return NULL;

	char* base = strerev(ls, sep, '/');

	if(sep - base < nlen + 3)
		return NULL;
	if(strncmp(base, name, nlen))
		return NULL;
	if(base[nlen] != '.')
		return NULL;

	return sep;
}

static void query_modules_dep(struct modctx* ctx, struct mbuf* mb)
{
	char* name = ctx->name;
	char* dir = ctx->dir;

	char dep[strlen(dir)+20];
	concat(dep, sizeof(dep), dir, "/modules.dep");

	mmapwhole(mb, dep);

	long dlen = mb->len;
	char* deps = mb->buf;
	char* dend = deps + dlen;

	char *ls, *le, *sep = NULL;
	int nlen = strlen(name);

	for(ls = le = deps; ls < dend; ls = le + 1) {
		le = eol(ls, dend);
		if((sep = match_depmod_line(ls, le, name, nlen)))
			break;
	} if(ls >= dend) {
		fail("module not found:", name, 0);
	}

	ctx->ls = ls;
	ctx->le = le;
	ctx->sep = sep;
}

/* modctx for a given module looks somewhat like this:

       kernel/fs/fat/vfat.ko.gz: kernel/fs/fat/fat.ko.gz
       ^                       ^                        ^
       ls                     sep                       le

   The line is not 0-terminated, so individual module names
   are passed as pointer:length pair. */


/* Module file names (base:blen here) in modules.dep are relative
   to the directory modules.dep resides in, which is ctx->dir. */

static void insmod_relative(struct modctx* ctx, char* bp, int bl, char* pars)
{
	char* dp = ctx->dir;
	int dl = strlen(dp);

	char *p, *e;

	char path[dl + bl + 4];
	p = path;
	e = path + sizeof(path) - 1;

	p = fmtstrn(p, e, dp, dl);
	*p++ = '/';
	p = fmtstrn(p, e, bp, bl);
	*p++ = '\0';

	insmod(path, pars, ctx->envp);
}

static void insmod_dependencies(struct modctx* ctx)
{
	char* p = ctx->sep + 1;
	char* e = ctx->le;

	while(p < e && isspace(*p)) p++;

	while(p < e) {
		char* q = p;

		while(q < e && !isspace(*q)) q++;

		insmod_relative(ctx, p, q - p, "");

		while(q < e &&  isspace(*q)) q++;

		p = q;
	}
}

static void insmod_primary_module(struct modctx* ctx)
{
	insmod_relative(ctx, ctx->ls, ctx->sep - ctx->ls, ctx->pars);
}

static void urelease(char* buf, int len)
{
	struct utsname uts;

	xchk(sysuname(&uts), "uname", NULL);

	int relen = strlen(uts.release);

	if(relen > len - 1)
		fail("release name too long:", uts.release, 0);

	memcpy(buf, uts.release, relen + 1);
}

void modprobe(char* name, char* pars, char** envp)
{
	char rel[MAX_RELEASE_LEN];

	urelease(rel, sizeof(rel));

	char dir[strlen(rel)+16];
	concat(dir, sizeof(dir), "/lib/modules/", rel);

	struct modctx ctx;
	memset(&ctx, 0, sizeof(ctx));

	ctx.dir = dir;
	ctx.name = name;
	ctx.envp = envp;
	ctx.pars = pars;

	struct mbuf deps;

	query_modules_dep(&ctx, &deps);
	insmod_dependencies(&ctx);
	insmod_primary_module(&ctx);

	munmapbuf(&deps);
}
