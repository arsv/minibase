#include <sys/module.h>
#include <sys/info.h>

#include <errtag.h>
#include <string.h>
#include <format.h>
#include <util.h>
#include <dirs.h>

#include "modprobe.h"

#define OPTS "ranqb"
#define OPT_r (1<<0)
#define OPT_a (1<<1)
#define OPT_n (1<<2)
#define OPT_q (1<<3)
#define OPT_b (1<<4)

ERRTAG("modprobe");
ERRLIST(NEACCES NEAGAIN NEBADF NEINVAL NENFILE NENODEV NENOMEM NEPERM NENOENT
	NETXTBSY NEOVERFLOW NEBADMSG NEBUSY NEFAULT NENOKEY NEEXIST NENOEXEC
	NESRCH);

static int got_args(CTX)
{
	return ctx->argi < ctx->argc;
}

static char* shift_arg(CTX)
{
	if(!got_args(ctx))
		return NULL;

	return ctx->argv[ctx->argi++];
}

static int check_strip_suffix(char* name, int nlen, char* suffix)
{
	int slen = strlen(suffix);

	if(nlen < slen)
		return 0;
	if(strncmp(name + nlen - slen, suffix, slen))
		return 0;

	name[nlen-slen] = '\0';
	return 1;
}

/* Naming convention:

      name: e1000e
      base: e1000e.ko.gz
      relpath: kernel/drivers/net/ethernet/intel/e1000e/e1000e.ko.gz
      path: /lib/modules/4.11.9/kernel/drivers/..../e1000e.ko.gz

   modprobe gets called with a name, most index files in /lib/modules
   use relpath, and open/mmap need full path. */

static void report_insmod(CTX, char* path, char* pars)
{
	int len1 = strlen(path);
	int len2 = pars ? strlen(pars) : 0;

	FMTBUF(p, e, cmd, 20 + len1 + len2);

	p = fmtstr(p, e, "insmod ");
	p = fmtstr(p, e, path);

	if(pars) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, pars);
	}

	FMTENL(p, e);

	writeall(STDOUT, cmd, p - cmd);
}

static void load_module(CTX, struct mbuf* mb, char* path, char* base, int blen)
{
	if(check_strip_suffix(base, blen, ".ko"))
		mmap_whole(mb, path, 1);
	else if(check_strip_suffix(base, blen, ".ko.gz"))
		decompress(ctx, mb, path, "/bin/zcat");
	else if(check_strip_suffix(base, blen, ".ko.xz"))
		decompress(ctx, mb, path, "/bin/xzcat");
	else
		fail("unexpected module extension:", base, 0);
}

static void cut_suffix(char* name)
{
	char* p = name;

	while(*p && *p != '.') p++;

	*p = '\0';
}

void insmod(CTX, char* relpath, char* pars)
{
	char* release = ctx->release;
	char* base = basename(relpath);
	int blen = strlen(base);
	char name[blen+1];

	memcpy(name, base, blen + 1);
	cut_suffix(name);

	FMTBUF(p, e, path, 40 + strlen(release) + strlen(relpath));
	p = fmtstr(p, e, "/lib/modules/");
	p = fmtstr(p, e, release);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, relpath);
	FMTEND(p, e);

	if(!pars)
		pars = query_pars(ctx, name);
	if(!pars)
		pars = "";

	if(ctx->opts & OPT_n)
		return report_insmod(ctx, path, pars);

	struct mbuf mb;
	int ret;

	load_module(ctx, &mb, path, base, blen);

	if(ctx->opts & OPT_n)
		return;

	if((ret = sys_init_module(mb.buf, mb.len, pars)) >= 0)
		;
	else if(ret == -EEXIST)
		;
	else fail("init-module", name, ret);

	unmap_buf(&mb);
}

static void report_rmmod(char* name)
{
	FMTBUF(p, e, line, strlen(name) + 20);
	p = fmtstr(p, e, "rmmod ");
	p = fmtstr(p, e, name);
	FMTENL(p, e);

	writeall(STDOUT, line, p - line);
}

static void remove_dep(CTX, char* path)
{
	char* base = basename(path);
	int blen = strlen(base);

	char buf[blen+1];
	memcpy(buf, base, blen+1);

	char* p = buf;
	while(*p && *p != '.') p++;
	*p = '\0';

	if(ctx->opts & OPT_n)
		report_rmmod(buf);
	else
		sys_delete_module(buf, 0);
}

static void try_remove_all(CTX, char** deps)
{
	char** p;

	for(p = deps + 1; *p; p++)
		remove_dep(ctx, *p);
}

static void remove(CTX, char* name)
{
	char **deps;
	int ret;

	if(!(deps = query_deps(ctx, name)))
		fail("cannot remove built-in", name, 0);

	try_remove_all(ctx, deps);

	if(ctx->opts & OPT_n)
		report_rmmod(name);
	else if((ret = sys_delete_module(name, 0)) < 0)
		fail(NULL, name, ret);

	flush_heap(ctx);
}

/* For the listed dependencies,

       mod: dep-a dep-b dep-c

   it looks like the right insertion order is dep-c, dep-b, dep-a,
   and the right removal order is the opposite. No clear indication
   whether it's true though. If it's not, then it's going to be multi
   pass insmod which I would rather avoid. */

static void insert_(CTX, char* name, char* pars)
{
	char** deps;
	char* alias;

	if((alias = query_alias(ctx, name)))
		name = alias;

	if(!(ctx->opts & OPT_b))
		;
	else if(is_blacklisted(ctx, name))
		return;

	if((deps = query_deps(ctx, name)))
		;
	else if(ctx->opts & (OPT_q | OPT_b))
		_exit(0xff);
	else
		fail(name, NULL, -ESRCH);

	char **p, **e;

	for(e = deps; *e; e++)
		;
	if(e == deps)
		return;

	for(p = e - 1; p >= deps + 1; p--)
		insmod(ctx, *p, NULL);

	insmod(ctx, deps[0], pars);

	flush_heap(ctx);
}

static void insert(CTX, char* name)
{
	insert_(ctx, name, NULL);
}

static void insert_single(CTX)
{
	char* name = shift_arg(ctx);
	char* pars = shift_arg(ctx);

	if(!name)
		fail("module name required", NULL, 0);
	if(got_args(ctx))
		fail("too many arguments", NULL, 0);

	insert_(ctx, name, pars);
}

static void forall_args(CTX, void (*call)(CTX, char* name))
{
	char* name;

	if(!got_args(ctx))
		fail("module name required", NULL, 0);

	while((name = shift_arg(ctx)))
		call(ctx, name);
}

static int parse_args(CTX, int argc, char** argv, char** envp)
{
	int i = 1, opts = 0;

	memzero(ctx, sizeof(*ctx));

	ctx->argc = argc;
	ctx->argv = argv;
	ctx->envp = envp;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i < argc && !strcmp(argv[i], "--"))
		i++;

	ctx->opts = opts;
	ctx->argi = i;

	return opts;
}

int main(int argc, char** argv, char** envp)
{
	struct top context, *ctx = &context;
	int opts = parse_args(ctx, argc, argv, envp);

	if(opts & OPT_r)
		forall_args(ctx, remove);
	else if(opts & OPT_a)
		forall_args(ctx, insert);
	else
		insert_single(ctx);

	return 0;
}
