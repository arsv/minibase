#include <sys/file.h>
#include <sys/dents.h>
#include <sys/mman.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <main.h>

#define PAGE 4096

#define OPTS "scbndai"
#define OPT_s (1<<0)	/* size individual dirents */
#define OPT_c (1<<1)	/* show total size */
#define OPT_b (1<<2)	/* -an */
#define OPT_n (1<<3)	/* show raw byte values, w/o KMG suffix */
#define OPT_d (1<<4)	/* directories only */
#define OPT_a (1<<5)	/* count apparent size */
#define OPT_i (1<<6)	/* in given directories */

#define SET_cwd (1<<8)

ERRTAG("du");

struct top {
	int opts;

	int argc;
	int argi;
	char** argv;

	char* brk;
	char* res;
	char* ptr;
	char* end;

	int count;
	int incomplete;

	uint64_t size;
	uint64_t total;
};

struct res {
	int len;
	uint64_t size;
	char name[];
};

struct rfn {
	int at;
	char* dir;
	char* name;
};

#define CTX struct top* ctx
#define FN struct rfn* fn
#define AT(dd) dd->at, dd->name

/* Heap routines. Each call to scan_top() may add a struct res to the heap,
   so that dump_results() could index and sort them later. Scanning may use
   some heap for dirent buffers, but any space allocated that way gets reset
   in scan_top() so that only the results are left. */

static void init_heap(CTX)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + 2*PAGE);

	if(brk_error(brk, end))
		fail("cannot allocate memory", NULL, 0);

	ctx->brk = brk;
	ctx->res = brk;
	ctx->ptr = brk;
	ctx->end = end;
}

static void heap_extend(CTX, long need)
{
	need += (PAGE - need % PAGE) % PAGE;
	char* req = ctx->end + need;
	char* new = sys_brk(req);

	if(mmap_error(new))
		fail("cannot allocate memory", NULL, 0);

	ctx->end = new;
}

static void* heap_alloc(CTX, int len)
{
	char* ptr = ctx->ptr;
	long avail = ctx->end - ptr;

	if(avail < len)
		heap_extend(ctx, len - avail);

	ctx->ptr += len;

	return ptr;
}

static long heap_left(CTX)
{
	return ctx->end - ctx->ptr;
}

/* Result sorting and final output */

static int sizecmp(void* pa, void* pb)
{
	struct res* a = pa;
	struct res* b = pb;

	if(a->size < b->size)
		return -1;
	if(a->size > b->size)
		return  1;

	return strcmp(a->name, b->name);
}

static struct res** index_results(CTX, int count)
{
	char* brk = ctx->brk;
	char* ptr = ctx->ptr;

	struct res** idx = heap_alloc(ctx, count*sizeof(struct res*));

	char* p;
	int i = 0;

	for(p = brk; p < ptr;) {
		struct res* rs = (struct res*) p;
		p += rs->len;

		if(i >= count)
			fail("count mismatch", NULL, 0);

		idx[i++] = rs;
	}

	qsortp(idx, count, sizecmp);

	return idx;
}

static void prep_bufout(CTX, struct bufout* bo)
{
	long len;

	if((len = heap_left(ctx)) < PAGE) {
		heap_extend(ctx, PAGE);
		len = heap_left(ctx);
	};

	bo->fd = STDOUT;
	bo->buf = heap_alloc(ctx, len);
	bo->len = len;
	bo->ptr = 0;
}

void dump_single(CTX, struct bufout* bo, struct res* rs)
{
	int opts = ctx->opts;

	FMTBUF(p, e, line, 50 + strlen(rs->name));

	if(opts & OPT_n)
		p = fmtpad(p, e, 8, fmtu64(p, e, rs->size));
	else
		p = fmtpad(p, e, 5, fmtsize(p, e, rs->size));

	if(rs->name[0]) {
		p = fmtstr(p, e, "  ");
		p = fmtstr(p, e, rs->name);
	}

	FMTENL(p, e);

	char* q = line;

	if(!(opts & OPT_s))
		while(*q == ' ') q++;

	bufout(bo, q, p - q);
}

void dump_total(CTX, struct bufout* bo)
{
	int opts = ctx->opts;

	FMTBUF(p, e, line, 80);

	if(opts & OPT_n)
		p = fmtpad(p, e, 8, fmtu64(p, e, ctx->total));
	else
		p = fmtpad(p, e, 5, fmtsize(p, e, ctx->total));

	if(opts & OPT_s)
		p = fmtstr(p, e, " total");

	FMTENL(p, e);

	bufout(bo, line, p - line);
}

void dump_results(CTX)
{
	int opts = ctx->opts;
	int count = ctx->count;
	struct res** idx = index_results(ctx, count);

	struct bufout bo;
	int i;

	prep_bufout(ctx, &bo);

	if(!(opts & OPT_s))
		;
	else for(i = 0; i < count; i++)
		dump_single(ctx, &bo, idx[i]);

	if(ctx->opts & OPT_c)
		dump_total(ctx, &bo);

	bufoutflush(&bo);

	if(ctx->incomplete)
		warn("incomplete results due to scan errors", NULL, 0);
}

/* Support routines for at-filenames */

static int pathlen(struct rfn* dd)
{
	char* name = dd->name;
	char* dir = dd->dir;
	int len = 0;

	if(name)
		len += strlen(name);

	if(name && name[0] == '/')
		;
	else if(dir)
		len += strlen(dir) + 1;

	return len + 1;
}

static void makepath(char* buf, int size, struct rfn* dd)
{
	char* p = buf;
	char* e = buf + size - 1;
	char* dir = dd->dir;
	char* name = dd->name;

	if(dir && (!name || name[0] != '/')) {
		p = fmtstr(p, e, dir);
		p = fmtstr(p, e, "/");
	} if(name) {
		p = fmtstr(p, e, name);
	}

	*p = '\0';
}

static void errin(CTX, struct rfn* dd, char* msg, int ret)
{
	char path[pathlen(dd)];

	makepath(path, sizeof(path), dd);

	warn(msg, path, ret);

	ctx->incomplete = 1;
}

/* Directory tree walking */

static void add_st_size(CTX, struct stat* st)
{
	if(ctx->opts & OPT_a)
		ctx->size += st->size;
	else
		ctx->size += st->blocks*512;
}

static void scan_entry(CTX, struct rfn* dd, int type, int* isdir);

static void scan_directory(CTX, struct rfn* dd, int fd)
{
	int plen = pathlen(dd);
	char path[plen];

	makepath(path, plen, dd);

	struct rfn next = { fd, path, NULL };

	char* ptr = ctx->ptr;
	int blen = 2096;
	char* buf = heap_alloc(ctx, blen);
	int rd;

	while((rd = sys_getdents(fd, buf, blen)) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;

			p += de->reclen;

			if(dotddot(de->name))
				continue;

			next.name = de->name;

			scan_entry(ctx, &next, de->type, NULL);
		}
	}

	ctx->ptr = ptr;

	sys_close(fd);
}

static void scan_entry(CTX, struct rfn* dd, int type, int* isdir)
{
	struct stat st;
	int ret, fd;

	if(type == DT_DIR) {
		if((fd = sys_openat(AT(dd), O_DIRECTORY)) < 0)
			return errin(ctx, dd, NULL, fd);
		if((ret = sys_fstat(fd, &st)) < 0)
			return errin(ctx, dd, "stat", ret);

		add_st_size(ctx, &st);

		if(isdir) *isdir = 1;

		scan_directory(ctx, dd, fd);
	} else {
		if((ret = sys_fstatat(AT(dd), &st, AT_SYMLINK_NOFOLLOW)) < 0)
			return errin(ctx, dd, "stat", ret);

		if((st.mode & S_IFMT) == S_IFDIR) {
			if((fd = sys_openat(AT(dd), O_DIRECTORY)) < 0)
				return errin(ctx, dd, NULL, fd);

			add_st_size(ctx, &st);

			if(isdir) *isdir = 1;

			scan_directory(ctx, dd, fd);
		} else if(isdir && (ctx->opts & OPT_d)) {
			; /* skip top-level non-directories */
		} else {
			add_st_size(ctx, &st);
		}
	}
}

static void scan_top(CTX, struct rfn* dd, int type)
{
	int opts = ctx->opts;
	char* ptr = ctx->ptr;
	int isdir = 0;

	scan_entry(ctx, dd, type, &isdir);

	if((opts & OPT_d) && !isdir)
		goto out;
	if(!(opts & OPT_s))
		goto sum;

	ctx->ptr = ptr;

	int plen = pathlen(dd) + isdir;
	int alen = sizeof(struct res) + plen;
	struct res* rs = heap_alloc(ctx, alen);

	makepath(rs->name, plen, dd);

	if(isdir && plen >= 2) {
		rs->name[plen-2] = '/';
		rs->name[plen-1] = '\0';
	}

	rs->len = alen;
	rs->size = ctx->size;
	ctx->count++;
sum:
	ctx->total += ctx->size;
out:
	ctx->size = 0;
}

static void scan_all_entries_in(CTX, int fd, char* dir)
{
	char buf[1024];
	int rd;

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;

			p += de->reclen;

			if(dotddot(de->name))
				continue;

			struct rfn dd = { fd, dir, de->name };

			scan_top(ctx, &dd, de->type);
		}
	}
}

/* Options and invocation */

static int got_args(CTX)
{
	return (ctx->argi < ctx->argc);
}

static char* shift_arg(CTX)
{
	if(ctx->argi >= ctx->argc)
		return NULL;

	return ctx->argv[ctx->argi++];
}

static void scan_dirs(CTX)
{
	char* dir;
	int fd;

	while((dir = shift_arg(ctx))) {
		if((fd = sys_open(dir, O_DIRECTORY)) < 0)
			fail(NULL, dir, fd);

		scan_all_entries_in(ctx, fd, dir);

		sys_close(fd);
	}
}

static void scan_each(CTX)
{
	char* name;

	while((name = shift_arg(ctx))) {
		struct rfn dd = { AT_FDCWD, NULL, name };
		scan_top(ctx, &dd, DT_UNKNOWN);
	}
}

static void scan_cwd(CTX)
{
	int fd;

	if((fd = sys_open(".", O_DIRECTORY)) < 0)
		fail("cannot open", ".", fd);

	scan_all_entries_in(ctx, fd, NULL);

	sys_close(fd);
}

static int parse_opts(CTX, int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->argi = i;
	ctx->argc = argc;
	ctx->argv = argv;

	int left = argc - i;

	if(opts & OPT_b)
		opts |= OPT_a | OPT_n;

	if(opts & (OPT_s | OPT_c))
		;
	else if(left == 1 && !(opts & OPT_i))
		opts |= OPT_c;
	else
		opts |= OPT_s;

	ctx->opts = opts;

	return opts;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	int opts = parse_opts(ctx, argc, argv);

	init_heap(ctx);

	if(opts & OPT_i)
		scan_dirs(ctx);
	else if(got_args(ctx))
		scan_each(ctx);
	else
		scan_cwd(ctx);

	dump_results(ctx);

	return 0;
}
