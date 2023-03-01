#include <sys/file.h>
#include <sys/info.h>
#include <sys/mman.h>

#include <config.h>
#include <format.h>
#include <printf.h>
#include <string.h>
#include <output.h>
#include <util.h>
#include <main.h>

#include "common.h"

ERRTAG("modinfo");

#define OPTS "abdlnps"
#define OPT_a (1<<0)
#define OPT_b (1<<1)
#define OPT_d (1<<2)
#define OPT_l (1<<3)
#define OPT_n (1<<4)
#define OPT_p (1<<5)
#define OPT_s (1<<6)

struct top {
	int opts;

	char* module;
	char* path;
	char* base;

	struct bufout bo;
	struct upac pc;
	struct mbuf modules_dep;

	int showpath;
};

struct entry {
	char* key; /* NOT zero-terminated */
	uint klen;
	char* value; /* zero-terminated */
	uint llen;
};

char outbuf[4096];

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

static char* next_entry(char* ptr, char* end, struct entry* ent)
{
	while(ptr < end && !*ptr)
		ptr++; /* skip empty entries */

	long left = end - ptr;
	long llen = strnlen(ptr, left);
	char* sep;

	if(ptr + llen >= end)
		return NULL;

	ent->key = ptr;
	ent->llen = llen;

	if(llen && (sep = strecbrk(ptr, ptr + llen, '=')) < end) {
		ent->klen = sep - ptr;
		ent->value = sep + 1;
	} else {
		ent->klen = 0;
		ent->value = ptr;
	}

	return ptr + llen + 1;
}

static int match_key(struct entry* en, char* key)
{
	return !strncmp(en->key, key, en->klen);
}

static void dump_modinfo(CTX, struct kmod* mod)
{
	char* ptr = mod->buf + mod->modinfo_off;
	char* end = ptr + mod->modinfo_len;
	struct entry en;

	while((ptr = next_entry(ptr, end, &en))) {
		if(match_key(&en, "alias"))
			continue;
		if(match_key(&en, "parm"))
			continue;
		if(match_key(&en, "parmtype"))
			continue;

		FMTBUF(p, e, buf, en.llen + 10);
		p = fmtstrn(p, e, en.key, en.klen);
		p = fmtstr(p, e, ": ");
		p = fmtstr(p, e, en.value);
		FMTENL(p, e);

		output(ctx, buf, p - buf);
	}
}

static void dump_single(CTX, struct kmod* mod, int keyed, char* key)
{
	char* ptr = mod->buf + mod->modinfo_off;
	char* end = ptr + mod->modinfo_len;
	struct entry en;

	while((ptr = next_entry(ptr, end, &en))) {
		if(!match_key(&en, key))
			continue;

		FMTBUF(p, e, buf, en.llen + 10);

		if(keyed) {
			p = fmtstrn(p, e, en.key, en.klen);
			p = fmtstr(p, e, ": ");
		}

		p = fmtstr(p, e, en.value);

		FMTENL(p, e);

		output(ctx, buf, p - buf);
	}
}

static char* get_parm_type(struct entry* enn)
{
	char* line = enn->value;
	char* sep = strecbrk(line, line + strlen(line), ':');

	return (*sep == ':') ? sep + 1 : NULL;
}

static void dump_parameters(CTX, struct kmod* mod)
{
	char* ptr = mod->buf + mod->modinfo_off;
	char* end = ptr + mod->modinfo_len;
	struct entry en, enn;
	char* ptrr;
	char* type;

	while((ptr = next_entry(ptr, end, &en))) {
		if(!match_key(&en, "parm"))
			continue;

		if(!(ptrr = next_entry(ptr, end, &enn)))
			;
		else if(!match_key(&enn, "parmtype"))
			ptrr = NULL;

		int need = en.llen + 10 + (ptrr ? enn.llen : 0);
		char* line = en.value;
		char* psep = strecbrk(line, line + strlen(line), ':');

		FMTBUF(p, e, buf, need);

		if(*psep == ':') {
			p = fmtstrn(p, e, line, psep - line);
			p = fmtstr(p, e, ": ");
			p = fmtstr(p, e, psep + 1);
		} else {
			p = fmtstr(p, e, line);
		}

		if(ptrr && (type = get_parm_type(&enn))) {
			p = fmtstr(p, e, " (");
			p = fmtstr(p, e, type);
			p = fmtstr(p, e, ")");
		}

		FMTENL(p, e);

		output(ctx, buf, p - buf);

		if(ptrr) ptr = ptrr;
	}
}

static void dump_full_path(CTX)
{
	char* path = ctx->path;
	int len = strlen(path);

	if(!ctx->showpath) return;

	path[len] = '\n';
	output(ctx, path, len + 1);
	path[len] = '\0';
}

static void empty_line(CTX)
{
	output(ctx, "\n", 1);
}

static int count_bits(uint val)
{
	int i, c = 0;

	for(i = 0; i < 32 && val; i++) {
		if(val & 1) c++;
		val = (val >> 1);
	}

	return c;
}

static void use_module_file(CTX, char* path)
{
	struct mbuf modbuf, *buf = &modbuf;
	struct kmod module, *mod = &module;
	char* base = basename(path);
	int opts = ctx->opts;
	int mask = OPT_a | OPT_d | OPT_p | OPT_l | OPT_n | OPT_s;
	int keyed = 0;
	int ret;

	ctx->path = path;

	if(opts & OPT_n)
		ctx->showpath = 1;
	if((opts & mask) == OPT_n) {
		dump_full_path(ctx);
		return;
	}

	if((ret = load_module(&ctx->pc, buf, path)) < 0)
		return;
	if((ret = find_modinfo(mod, buf, base)) < 0)
		return;

	if(!(opts & mask)) {
		dump_full_path(ctx);
		dump_modinfo(ctx, mod);
		return;
	}
	if(count_bits(opts & (OPT_d | OPT_a | OPT_l)) > 1)
		keyed = 1;
	if(opts & OPT_n)
		dump_full_path(ctx);
	if(opts & OPT_d)
		dump_single(ctx, mod, keyed, "description");
	if(opts & OPT_a)
		dump_single(ctx, mod, keyed, "author");
	if(opts & OPT_l)
		dump_single(ctx, mod, keyed, "license");

	if(opts & OPT_p) {
		if(opts & (OPT_l | OPT_a))
			empty_line(ctx);
		dump_parameters(ctx, mod);
	}
	if(opts & OPT_s) {
		if(opts & (OPT_l | OPT_a | OPT_p))
			empty_line(ctx);
		dump_single(ctx, mod, 0, "alias");
	}
}

static int match_mod(char* name, int nlen, char* line, int llen)
{
	int s, e;

	if(!llen)
		return 0;

	for(s = llen-1; s > 0; s--)
		if(line[s-1] == '/')
			break;

	for(e = s; e < llen; e++)
		if(line[e] == '.')
			break;

	if(e - s != nlen)
		return 0;

	return !memcmp(name, line + s, nlen);
}

static void scan_modules_dep(CTX, struct mbuf* mb, char* base, char* name)
{
	int nlen = strlen(name);

	char* ptr = mb->buf;
	char* end = ptr + mb->len;
	char *ls, *sep, *le;

	while(ptr < end) {
		ls = ptr;
		le = strecbrk(ls, end, '\n');
		sep = strecbrk(ls, le, ':');
		ptr = le + 1;

		if(ls >= le || sep >= le)
			continue;
		if(match_mod(name, nlen, ls, sep - ls))
			break;
	} if(ptr >= end) {
		fail("cannot find module", name, 0);
	}

	FMTBUF(p, e, path, strlen(base) + (sep - ls) + 10);
	p = fmtstr(p, e, base);
	p = fmtstr(p, e, "/");
	p = fmtraw(p, e, ls, sep - ls);
	FMTEND(p, e);

	use_module_file(ctx, path);
}

static void load_modules_dep(CTX, struct mbuf* mb, char* base)
{
	FMTBUF(p, e, path, strlen(base) + 50);
	p = fmtstr(p, e, base);
	p = fmtstr(p, e, "/modules.dep");
	FMTEND(p, e);

	mmap_whole(mb, path, REQ);
}

static void locate_by_name(CTX, char* name)
{
	char* base = ctx->base;
	struct mbuf* mb = &ctx->modules_dep;

	ctx->showpath = 1;

	if(!base) {
		int ret;
		struct utsname ut;

		if((ret = sys_uname(&ut)) < 0)
			fail("uname", NULL, ret);

		uint size = 100;
		char* buf = alloca(size);
		char* p = buf;
		char* e = buf + size - 1;

		p = fmtstr(p, e, "/lib/modules/");
		p = fmtstr(p, e, ut.release);

		base = buf;
	}

	load_modules_dep(ctx, mb, base);
	scan_modules_dep(ctx, mb, base, name);
}

static int looks_like_file(char* name)
{
	const char* p;

	for(p = name; *p; p++)
		if(*p == '/' || *p == '.')
			return 1;

	return 0;
}

int main(int argc, char** argv)
{
	int opts = 0, i = 1;
	struct top context, *ctx = &context;
	char* module;

	memzero(ctx, sizeof(*ctx));

	ctx->pc.envp = argv + argc + 1;
	ctx->pc.sdir = BASE_ETC "/pac";

	ctx->bo.fd = STDOUT;
	ctx->bo.buf = outbuf;
	ctx->bo.len = sizeof(outbuf);

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc && (opts & OPT_b))
		ctx->base = argv[i++];
	if(i < argc)
		module = argv[i++];
	else
		fail("too few arguments", NULL, 0);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	ctx->opts = opts;
	ctx->module = module;

	if(looks_like_file(module))
		use_module_file(ctx, module);
	else
		locate_by_name(ctx, module);

	bufoutflush(&ctx->bo);

	return 0;
}
