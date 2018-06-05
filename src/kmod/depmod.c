#include <sys/file.h>
#include <sys/mman.h>
#include <sys/info.h>
#include <sys/dents.h>
#include <sys/fpath.h>

#include <format.h>
#include <string.h>
#include <output.h>
#include <main.h>
#include <util.h>

#include "common.h"

ERRTAG("depmod");

#define OPTS "v"
#define OPT_v (1<<0)

struct mod {
	uint len;
	uint dlen;
	uint slen;
	char* deps;
	char path[];
};

struct dep {
	char* name;
	int nlen;
};

struct top {
	char** envp;

	int opts;
	char* base;

	int nofail;

	char* brk;
	char* ptr;
	char* end;

	int nmods;

	struct mod** pidx;
	struct mod** nidx;

	struct dep seen[50];
	int sptr;
	int transitive;

	struct mbuf builtin;
	struct bufout mdep;
	struct bufout mali;

	int failed;
};

char depbuf[2048];
char alibuf[2048];

char** environ(CTX)
{
	return ctx->envp;
}

int error(CTX, const char* msg, char* arg, int err)
{
	if(!ctx->nofail) {
		warn(msg, arg, err);
		ctx->failed = 1;
	}
	return err ? err : -1;
}

static void report_unresolved(CTX, struct mod* md, char* name, int nlen)
{
	char* base = md->path + md->dlen + 1;

	FMTBUF(p, e, buf, nlen + 50);
	p = fmtstr(p, e, "needs ");
	p = fmtstrn(p, e, name, nlen);
	FMTEND(p, e);

	warn(base, buf, 0);

	ctx->failed = 1;
}

static void output(struct bufout* bo, char* s)
{
	bufout(bo, s, strlen(s));
}

static void init_heap(CTX)
{
	void* brk = sys_brk(NULL);
	int ret;

	if((ret = mmap_error(brk)))
		fail("cannot initialize heap:", NULL, ret);

	ctx->brk = brk;
	ctx->end = brk;
	ctx->ptr = brk;
}

static void* halloc(CTX, long size)
{
	void* ptr = ctx->ptr;
	void* end = ctx->end;
	void* new = ptr + size;
	int ret;

	if(new > end) {
		void* ext = sys_brk(end + pagealign(new - end));

		if((ret = brk_error(end, ext)))
			fail("cannot allocate memory:", NULL, ret);

		ctx->end = ext;
	}

	ctx->ptr = new;

	return ptr;
}

static char* hstrdup(CTX, char* str)
{
	int len = strlen(str);
	char* dup = halloc(ctx, len + 1);
	memcpy(dup, str, len);
	dup[len] = '\0';
	return dup;
}

static int eq(char c)
{
	return c == '_' ? '-' : (c & 0xFF);
}

static int eqstrnncmp(char* a, int an, char* b, int bn)
{
	int n = an < bn ? an : bn;

	while(n-- > 0) {
		char ai = *a++;
		char bi = *b++;
		int d = eq(ai) - eq(bi);
		if(d) return d;
	}

	if(an < bn)
		return -1;
	if(an > bn)
		return  1;

	return 0;
}

/* Directory scanning section.
   Locate all available modules and build sorted indexes in ctx. */

static int by_path(const void* pa, const void* pb)
{
	struct mod* a = *((struct mod**) pa);
	struct mod* b = *((struct mod**) pb);

	return strcmp(a->path, b->path);
}

static int by_name(const void* pa, const void* pb)
{
	struct mod* a = *((struct mod**) pa);
	struct mod* b = *((struct mod**) pb);

	char* aname = a->path + a->dlen + 1;
	int anlen = a->slen;
	char* bname = b->path + b->dlen + 1;
	int bnlen = b->slen;

	return eqstrnncmp(aname, anlen, bname, bnlen);
}

static void index_modules(CTX)
{
	void* ptr = ctx->brk;
	void* end = ctx->ptr;
	int i = 0, n = ctx->nmods;
	int ptrsize = sizeof(struct mod*);

	struct mod** pidx = halloc(ctx, n*ptrsize);

	while(ptr < end) {
		struct mod* md = ptr;
		if(!md->len) break;
		ptr += md->len;
		pidx[i++] = md;
	}

	struct mod** nidx = halloc(ctx, n*ptrsize);

	memcpy(nidx, pidx, n*ptrsize);

	ctx->pidx = pidx;
	ctx->nidx = nidx;

	qsort(pidx, n, ptrsize, by_path);
	qsort(nidx, n, ptrsize, by_name);
}

static int stem_length(char* file)
{
	char* p = file;

	while(*p && *p != '.') p++;

	return p - file;
}

static void note_module(CTX, char* dir, int dlen, char* file)
{
	int flen = strlen(file);
	int plen = dlen + 1 + flen;
	int size = sizeof(struct mod) + plen + 1;
	struct mod* md = halloc(ctx, size);

	md->len = size;
	md->dlen = dlen;
	md->slen = stem_length(file);

	char* p = md->path;
	char* e = p + plen;
	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, file);
	*p = '\0';

	ctx->nmods++;
}

static int got_ko_extension(char* name)
{
	char* p = name;

	while(*p && *p != '.')
		p++;
	if(!*p)
		return 0;

	if(strncmp(p, ".ko", 3))
		return 0;
	if(p[3] && p[3] != '.')
		return 0;

	return 1;
}

static void scan_subdir(CTX, int at, char* prec, char* name);

static void scan_recurse(CTX, int at, char* prec, char* name)
{
	FMTBUF(p, e, path, strlen(prec) + strlen(name) + 2);
	p = fmtstr(p, e, prec);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	scan_subdir(ctx, at, path, name);
}

static void scan_subdir(CTX, int at, char* full, char* dir)
{
	int fd, rd;
	char buf[2048];

	if((fd = sys_openat(at, dir, O_DIRECTORY)) >= 0)
		;
	else if(fd == -ENOTDIR)
		return;
	else if(fd == -ENOENT && !full)
		return;
	else fail(NULL, dir, fd);

	if(!full) full = dir;

	int flen = strlen(full);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;
			if(!de->reclen) break;
			ptr += de->reclen;

			char* name = de->name;
			int type = de->type;

			if(dotddot(name))
				continue;

			if(got_ko_extension(name)) {
				if(type != DT_REG && type != DT_UNKNOWN)
					continue;
				note_module(ctx, full, flen, name);
			} else {
				if(type != DT_DIR && type != DT_UNKNOWN)
					continue;
				scan_recurse(ctx, fd, full, name);
			}
		}

	}

	sys_close(fd);
}

static void scan_modules(CTX)
{
	char* dir = ctx->base;
	int fd, rd, ret;
	char buf[512];

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);
	if((ret = sys_fchdir(fd)) < 0)
		fail("chdir", dir, ret);

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;
			if(!de->reclen) break;
			ptr += de->reclen;
			char* name = de->name;
			int type = de->type;

			if(dotddot(name))
				continue;
			if(type == DT_UNKNOWN)
				;
			else if(type == DT_LNK)
				;
			else if(type == DT_DIR)
				;
			else continue;

			scan_subdir(ctx, fd, NULL, name);
		}
	}
}

static int kernel_builtin(CTX, char* name, int nlen)
{
	struct mbuf* mb = &ctx->builtin;

	if(!mb->buf)
		return 0;

	char* p = mb->buf;
	char* e = p + mb->len;
	char* q = NULL;

	while(p < e) {
		char c = *p++;

		if(c == '/')
			q = p;
		if(c != '\n')
			continue;

		char* s = q;
		while(s < p && *s != '.') s++;
		int len = s - q;

		if(eqstrnncmp(name, nlen, q, len))
			continue;

		return 1;
	}

	return 0;
}

/* Dependency resolution section.
   Load each indexed module and find full paths of its dependencies. */

struct entry {
	char* key; /* NOT zero-terminated */
	uint klen;
	char* value; /* zero-terminated */
	uint llen;
};

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

static char* get_info_entry(CTX, struct kmod* km, char* key)
{
	char* ptr = km->buf + km->modinfo_off;
	char* end = ptr + km->modinfo_len;
	struct entry en;

	while((ptr = next_entry(ptr, end, &en)))
		if(match_key(&en, key))
			return en.value;

	return NULL;
}

static void dump_module_aliases(CTX, struct kmod* km, char* name)
{
	struct bufout* bo = &ctx->mali;
	char* ptr = km->buf + km->modinfo_off;
	char* end = ptr + km->modinfo_len;
	struct entry en;

	while((ptr = next_entry(ptr, end, &en))) {
		if(!match_key(&en, "alias"))
			continue;
		output(bo, "alias ");
		output(bo, en.value);
		output(bo, " ");
		output(bo, name);
		output(bo, "\n");
	}
}

static void check_name_stem_match(CTX, struct mod* md, char* name)
{
	char* stem = md->path + md->dlen + 1;
	int slen = md->slen;
	int nlen = strlen(name);

	if(!eqstrnncmp(stem, slen, name, nlen))
		return;

	FMTBUF(p, e, buf, strlen(name) + 50);
	p = fmtstr(p, e, "mis-matching name");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	warn(md->path, buf, 0);
}

static void process_module(CTX, struct mod* md)
{
	char* path = md->path;

	if(ctx->opts & OPT_v)
		warn(NULL, path, 0);

	struct mbuf modbuf, *buf = &modbuf;
	struct kmod module, *mod = &module;
	int ret;
	char *name, *deps;

	if((ret = load_module(ctx, buf, path)) < 0)
		return;
	if((ret = find_modinfo(ctx, mod, buf, basename(path))) < 0)
		return;

	if((deps = get_info_entry(ctx, mod, "depends")))
		md->deps = hstrdup(ctx, deps);

	if((name = get_info_entry(ctx, mod, "name"))) {
		check_name_stem_match(ctx, md, name);
	} else {
		int len = md->slen;
		char buf[len + 1];
		memcpy(buf, md->path + md->dlen + 1, len);
		buf[len] = '\0';
		name = buf;
	}

	dump_module_aliases(ctx, mod, name);
}

static struct mod* find_indexed_module(CTX, char* name, uint nlen)
{
	struct mod** nidx = ctx->nidx;
	int n = ctx->nmods;
	int l = 0;
	int r = n;

	while(l < r) {
		int m = l + (r - l)/2;

		struct mod* md = nidx[m];
		char* base = md->path + md->dlen + 1;
		int blen = md->slen;

		int rel = eqstrnncmp(base, blen, name, nlen);

		if(rel == 0)
			return md;
		if(rel > 0)
			r = m;
		else if(l == m)
			break;
		else
			l = m;
	}

	return NULL;
}

static int seen_this_module(CTX, char* name, int nlen)
{
	uint i, n = ctx->sptr;
	struct dep* seen = ctx->seen;

	for(i = 0; i < n; i++) {
		struct dep* dp = &seen[i];

		if(dp->nlen != nlen)
			continue;
		if(strncmp(dp->name, name, nlen))
			continue;

		return 1;
	}

	if(n >= ARRAY_SIZE(ctx->seen)) {
		warn("out of seen slots", NULL, 0);
	} else {
		ctx->seen[n].name = name;
		ctx->seen[n].nlen = nlen;
		ctx->sptr++;
	}

	return 0;
}

static void try_resolve_deps(CTX, struct mod* md);

static void try_resolve_name(CTX, struct mod* md, char* name, int nlen)
{
	struct mod* dd;

	if(seen_this_module(ctx, name, nlen))
		return;

	if(!(dd = find_indexed_module(ctx, name, nlen))) {
		if(ctx->transitive)
			;
		else if(kernel_builtin(ctx, name, nlen))
			;
		else report_unresolved(ctx, md, name, nlen);

		return;
	}

	struct bufout* bo = &ctx->mdep;

	output(bo, " ");
	output(bo, dd->path);

	ctx->transitive = 1;

	try_resolve_deps(ctx, dd);
}

static void try_resolve_deps(CTX, struct mod* md)
{
	char* deps = md->deps;

	if(!deps) return;

	char* p = deps;
	char* e = p + strlen(p);

	while(p < e) {
		char* q = strecbrk(p, e, ',');
		try_resolve_name(ctx, md, p, q - p);
		p = q + 1;
	}
}

static void resolve_module(CTX, struct mod* md)
{
	struct bufout* bo = &ctx->mdep;

	output(bo, md->path);
	output(bo, ":");

	ctx->sptr = 0;
	ctx->transitive = 0;

	try_resolve_deps(ctx, md);

	output(bo, "\n");
}

static void process_index(CTX)
{
	int i, n = ctx->nmods;
	struct mod** pidx = ctx->pidx;

	for(i = 0; i < n; i++)
		process_module(ctx, pidx[i]);

	if(ctx->opts & OPT_v)
		warn("* resolving dependencies", NULL, 0);
	for(i = 0; i < n; i++)
		resolve_module(ctx, pidx[i]);
}

static void load_builtin(CTX)
{
	ctx->nofail = 1;

	mmap_whole(ctx, &ctx->builtin, "modules.builtin");

	ctx->nofail = 0;
}

static void open_out_file(CTX, struct bufout* bo, char* name)
{
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;
	int fd;

	FMTBUF(p, e, temp, strlen(name) + 8);
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".tmp");
	FMTEND(p, e);

	if((fd = sys_open3(temp, flags, mode)) < 0) {
		char* base = ctx->base;

		FMTBUF(p, e, full, strlen(temp) + strlen(base) + 2);
		p = fmtstr(p, e, base);
		p = fmtstr(p, e, "/");
		p = fmtstr(p, e, temp);
		FMTEND(p, e);

		fail(NULL, full, fd);
	}

	bo->fd = fd;
}

static void fini_out_file(CTX, struct bufout* bo, char* name)
{
	int ret;

	FMTBUF(p, e, temp, strlen(name) + 8);
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".tmp");
	FMTEND(p, e);

	bufoutflush(bo);

	if((ret = sys_rename(temp, name)) < 0) {
		if(ret == -ENOENT)
			fail(NULL, temp, ret);
		else
			fail(NULL, name, ret);
	}
}

static void set_out_buf(struct bufout* bo, char* buf, int len)
{
	bo->buf = buf;
	bo->len = len;
}

static void init_output(CTX)
{
	set_out_buf(&ctx->mdep, depbuf, sizeof(depbuf));
	set_out_buf(&ctx->mali, alibuf, sizeof(alibuf));

	open_out_file(ctx, &ctx->mdep, "modules.dep");
	open_out_file(ctx, &ctx->mali, "modules.alias");
}

static void fini_output(CTX)
{
	fini_out_file(ctx, &ctx->mdep, "modules.dep");
	fini_out_file(ctx, &ctx->mali, "modules.alias");
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int i = 1, opts = 0;
	char* base;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->opts = opts;
	ctx->envp = argv + argc + 1;

	if(i < argc) {
		base = argv[i++];
	} else {
		struct utsname ut;
		int ret;

		if((ret = sys_uname(&ut)) < 0)
			fail("uname", NULL, ret);

		FMTBUF(p, e, basebuf, 20 + sizeof(ut.release));
		p = fmtstr(p, e, "/lib/modules/");
		p = fmtstr(p, e, ut.release);
		FMTEND(p, e);

		base = basebuf;
	}
	if(i < argc)
		fail("too many arguments", NULL, 0);

	ctx->base = base;

	init_heap(ctx);
	scan_modules(ctx);
	init_output(ctx);
	load_builtin(ctx);
	index_modules(ctx);
	process_index(ctx);
	fini_output(ctx);

	return ctx->failed ? 1 : 0;
}
