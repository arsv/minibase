#include <sys/module.h>
#include <sys/info.h>
#include <sys/file.h>

#include <config.h>
#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#include "common.h"
#include "modprobe.h"

#define OPTS "ranqbpvi"
#define OPT_r (1<<0)
#define OPT_a (1<<1)
#define OPT_n (1<<2)
#define OPT_q (1<<3)
#define OPT_b (1<<4)
#define OPT_p (1<<5)
#define OPT_v (1<<6)
#define OPT_i (1<<7)

ERRTAG("modprobe");
ERRLIST(NEACCES NEAGAIN NEBADF NEINVAL NENFILE NENODEV NENOMEM NEPERM NENOENT
	NETXTBSY NEOVERFLOW NEBADMSG NEBUSY NEFAULT NENOKEY NEEXIST NENOEXEC
	NESRCH);

int error(CTX, const char* msg, char* arg, int err)
{
	int ret = err ? err : -1;
	int opts = ctx->opts;

	if(!(opts & OPT_p) || (opts & OPT_v))
		warn(msg, arg, err);

	if(!(opts & (OPT_a | OPT_p)))
		_exit(0xFF);

	return ret;
}

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

static int mmap_modules_file(CTX, struct mbuf* mb, char* name, int optional)
{
	char* base = ctx->base;

	FMTBUF(p, e, path, strlen(base) + strlen(name) + 4);
	p = fmtstr(p, e, base);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	return mmap_whole(mb, path, optional);
}

static int prep_modules_alias(CTX)
{
	struct mbuf* mb = &ctx->modules_alias;
	char* name = "modules.alias";

	return mmap_modules_file(ctx, mb, name, OPT);
}

static int prep_modules_builtin(CTX)
{
	struct mbuf* mb = &ctx->modules_builtin;
	char* name = "modules.builtin";

	return mmap_modules_file(ctx, mb, name, OPT);
}

static int prep_config(CTX)
{
	struct mbuf* mb = &ctx->config;
	char* name = BASE_ETC "/modules";

	return mmap_whole(mb, name, OPT);
}

static int query_deps(CTX, struct line* ln, char* mod)
{
	struct mbuf* mb = &ctx->modules_dep;
	char* name = "modules.dep";

	if(mmap_modules_file(ctx, mb, name, REQ) < 0)
		return -1;

	return locate_dep_line(mb, ln, mod);
}

static int query_pars(CTX, struct line* ln, char* name)
{
	int ret;
	struct mbuf* mb = &ctx->config;

	if((ret = prep_config(ctx)) < 0)
		return ret;

	return locate_opt_line(mb, ln, name);
}

static int query_alias(CTX, struct line* ln, char* mod)
{
	struct mbuf* ma = &ctx->modules_alias;
	struct mbuf* cf = &ctx->config;
	int ret;

	prep_config(ctx);

	if((ret = locate_alias_line(cf, ln, mod)) >= 0)
		return ret;

	if((ret = prep_modules_alias(ctx)) < 0)
		return ret;

	return locate_alias_line(ma, ln, mod);
}

static int blacklisted(CTX, char* name)
{
	struct mbuf* mb = &ctx->config;
	struct line ln;

	if(!(ctx->opts & (OPT_q | OPT_p)))
		return 0;

	prep_config(ctx);

	if(locate_blist_line(mb, &ln, name) < 0)
		return 0;

	error(ctx, "blacklisted module", name, 0);

	return 1;
}

static int builtin(CTX, char* name)
{
	struct mbuf* mb = &ctx->modules_builtin;
	struct line ln;

	prep_modules_builtin(ctx);

	if(locate_built_line(mb, &ln, name) < 0)
		return 0;

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

static void remove_named(CTX, char* name)
{
	int opts = ctx->opts;
	int ret;

	if(opts & (OPT_n | OPT_v)) {
		FMTBUF(p, e, line, strlen(name) + 20);
		p = fmtstr(p, e, "rmmod ");
		p = fmtstr(p, e, name);
		FMTENL(p, e);

		writeall(STDOUT, line, p - line);
	}

	if(ctx->opts & OPT_n)
		return;

	if((ret = sys_delete_module(name, 0)) < 0)
		fail(NULL, name, ret);
}

static void remove_relative(CTX, char* ptr, char* end)
{
	char* base = ptr;
	char* p;

	for(p = ptr; p < end; p++)
		if(*p == '/')
			base = p + 1;
	for(p = base; p < end; p++)
		if(*p == '.')
			break;

	int len = p - base;

	if(len <= 0) return;

	char name[len+1];

	memcpy(name, base, len);
	name[len] = '\0';

	return remove_named(ctx, name);
}

static void remove(CTX, char* name)
{
	struct line ln;

	if(query_deps(ctx, &ln, name) < 0)
		goto mod;

	char* p = ln.val;
	char* e = ln.end;
	char* q;

	while(p < e) {
		while(p < e && isspace(*p)) p++;
		q = p;
		while(p < e && !isspace(*p)) p++;

		remove_relative(ctx, q, p);
	}
mod:
	remove_named(ctx, name);
}

/* For the listed dependencies,

       mod: dep-a dep-b dep-c

   it looks like the right insertion order is dep-c, dep-b, dep-a,
   and the right removal order is the opposite. No clear indication
   whether it's true though. If it's not, then it's going to be multi
   pass insmod which I would rather avoid. */

static int insert_absolute(CTX, char* name, char* path, char* pars)
{
	struct mbuf mb;
	int ret;

	if(ctx->opts & (OPT_v | OPT_n))
		report_insmod(ctx, path, pars);
	if(ctx->opts & OPT_n)
		return 0;

	if((ret = load_module(&ctx->pac, &mb, path)) < 0)
		return ret;

	if((ret = sys_init_module(mb.buf, mb.len, pars)) >= 0)
		;
	else if(ret == -EEXIST)
		;
	else return error(ctx, "init-module", name, ret);

	munmap_buf(&mb);

	return 0;
}

static int insert_w_pars(CTX, char* name, char* rptr, char* rend, char* pars)
{
	char* base = ctx->base;
	long rlen = rend - rptr;

	if(rlen < 0) return -EINVAL;

	FMTBUF(p, e, path, 4 + strlen(base) + rlen);
	p = fmtstr(p, e, ctx->base);
	p = fmtstr(p, e, "/");
	p = fmtstrn(p, e, rptr, rlen);
	FMTEND(p, e);

	return insert_absolute(ctx, name, path, pars);
}

static int insert_relative(CTX, char* name, char* rptr, char* rend, char* pars)
{
	struct line ln;

	if(pars != NULL) /* use them as is */
		return insert_w_pars(ctx, name, rptr, rend, pars);
	if(query_pars(ctx, &ln, name) < 0) /* no parameters */
		return insert_w_pars(ctx, name, rptr, rend, "");

	long len = ln.end - ln.val;

	if(len < 0 || len > 1024)
		return error(ctx, "invalid options for", name, 0);

	char parbuf[len+1];
	memcpy(parbuf, ln.val, len);
	parbuf[len] = '\0';

	return insert_w_pars(ctx, name, rptr, rend, parbuf);
}

static int insert_one_dep(CTX, char* ptr, char* end)
{
	char* base = ptr;
	char* p;

	for(p = ptr; p < end; p++)
		if(*p == '/')
			base = p + 1;
	for(p = base; p < end; p++)
		if(*p == '.')
			break;

	int len = p - base;
	char name[len+1];

	memcpy(name, base, len);
	name[len] = '\0';

	return insert_relative(ctx, name, ptr, end, NULL);
}

static int insert_dependencies(CTX, char* deps, char* dend)
{
	int ret;
	char* p = dend;
	char* q;

	while(p > deps) {
		while(p > deps && isspace(*(p-1)))
			p--;
		if(p == deps)
			break;

		q = p--;
		while(q > deps && !isspace(*(p-1)))
			p--;

		if((ret = insert_one_dep(ctx, p, q)) < 0)
			return ret;
	}

	return 0;
}

static void insert_named(CTX, char* name, char* pars)
{
	struct line ln;

	ctx->nmatching++;

	if(blacklisted(ctx, name))
		return;

	if(query_deps(ctx, &ln, name) < 0) {
		if(builtin(ctx, name)) {
			if(ctx->opts & OPT_v)
				warn("built-in module", name, 0);
			ctx->ninserted++;
			return;
		}

		error(ctx, "unknown module", name, 0);
		return;
	}

	if(insert_dependencies(ctx, ln.val, ln.end) < 0)
		return;
	if(insert_relative(ctx, name, ln.ptr, ln.sep, pars) < 0)
		return;

	ctx->ninserted++;
}

static void insert(CTX, char* name, char* pars)
{
	struct line ln;

	if(query_alias(ctx, &ln, name) < 0) /* not an alias */
		return insert_named(ctx, name, pars);

	FMTBUF(p, e, real, 256);
	p = fmtstrn(p, e, ln.val, ln.end - ln.val);
	FMTEND(p, e);

	return insert_named(ctx, real, pars);
}

static void insert_one(CTX)
{
	char* name = shift_arg(ctx);
	char* pars = shift_arg(ctx);

	if(!name)
		fail("module name required", NULL, 0);
	if(got_args(ctx))
		fail("too many arguments", NULL, 0);

	return insert(ctx, name, pars);
}

static void insert_all(CTX)
{
	char* name;

	if(!got_args(ctx))
		fail("too few arguments", NULL, 0);

	while((name = shift_arg(ctx)))
		insert(ctx, name, NULL);
}

static void remove_all(CTX)
{
	char* name;

	if(!got_args(ctx))
		fail("too few arguments", NULL, 0);

	while((name = shift_arg(ctx)))
		remove(ctx, name);
}

static void read_stdin(CTX)
{
	char buf[1024];
	int len = sizeof(buf);
	int off = 0;
	int rd;

	if(ctx->opts & (OPT_r | OPT_a))
		fail("cannot use -r or -a with -p", NULL, 0);

	ctx->opts |= OPT_a;

	while((rd = sys_read(STDIN, buf + off, len - off)) > 0) {
		char* e = buf + off + rd;
		char* p = buf;
		char* q;

		while(p < e) {
			if((q = strecbrk(p, e, '\n')) >= e)
				break;
			*q = '\0';
			insert(ctx, p, NULL);
			p = q + 1;
		}

		if(p > buf) {
			off = e - p;
			memmove(buf, p, off);
		}
	}
}

static void prep_base_path(char* buf, int len)
{
	struct utsname ut;
	int ret;

	if((ret = sys_uname(&ut)) < 0)
		fail("uname", NULL, ret);

	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, "/lib/modules/");
	p = fmtstr(p, e, ut.release);
	*p = '\0';
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int i = 1, opts = 0;

	memzero(ctx, sizeof(*ctx));

	ctx->argc = argc;
	ctx->argv = argv;

	ctx->pac.envp = argv + argc + 1;
	ctx->pac.sdir = BASE_ETC "/pac";

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->opts = opts;
	ctx->argi = i;

	if(opts & OPT_b) {
		ctx->base = shift_arg(ctx);
	} else if(opts & OPT_i) {
		ctx->base = "/lib/modules";
		ctx->pac.sdir = INIT_ETC "/pac";
	} else {
		int basemax = 100;
		char* basebuf = alloca(basemax);
		prep_base_path(basebuf, basemax);
		ctx->base = basebuf;
	}

	if(opts & OPT_p)
		read_stdin(ctx);
	else if(opts & OPT_r)
		remove_all(ctx);
	else if(opts & OPT_a)
		insert_all(ctx);
	else
		insert_one(ctx);

	if(ctx->nmatching && !ctx->ninserted)
		return 0xFF;

	return 0;
}
