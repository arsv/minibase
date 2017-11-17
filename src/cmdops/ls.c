#include <sys/file.h>
#include <sys/dents.h>
#include <sys/mman.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <output.h>
#include <util.h>

ERRTAG("ls");

#define MAYBEDIR  0
#define MUSTBEDIR 1

#define DT_LNK_DIR 71	/* symlink pointing to a dir, custom value */

#define OPTS "aubreyw"
#define OPT_a (1<<0)	/* show all files, including hidden ones */
#define OPT_u (1<<1)	/* uniform listing, dirs and filex intermixed */
#define OPT_b (1<<2)	/* basename listing, do not prepend argument */
#define OPT_r (1<<3)	/* recurse into subdirectories */
#define OPT_e (1<<4)	/* list leaf entries (non-dirs) only */
#define OPT_y (1<<5)	/* list symlinks as files, regardless of target */
#define OPT_w (1<<6)	/* follow symlinks */

#define SET_stat (1<<16) /* do stat() entries */

struct top {
	int opts;
	struct bufout bo;

	void* base;
	void* ptr;
	void* end;
};

struct dir {
	struct top* top;
	int fd;
	char* name;
};

#define CTX struct top* ctx
#define DCT struct dir* dct

char output[PAGE];

static void init(CTX, int opts)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + PAGE);

	if(brk_error(brk, end))
		fail("cannot initialize heap", NULL, 0);

	if(opts & OPT_e)
		opts |= SET_stat; /* need type before entering the dir */
	else if(!(opts & (OPT_r | OPT_u)))
		opts |= SET_stat; /* non-recurse, non-uniform list needs stat */
	else if(opts & OPT_y)
		; /* no need to stat symlinks */
	else
		opts |= SET_stat;

	ctx->opts = opts;

	ctx->base = brk;
	ctx->end = end;
	ctx->ptr = brk;

	ctx->bo.fd = 1;
	ctx->bo.buf = output;
	ctx->bo.len = sizeof(output);
	ctx->bo.ptr = 0;
}

static void fini(CTX)
{
	bufoutflush(&ctx->bo);
}

static void extend(CTX, long ext)
{
	if(ctx->ptr + ext < ctx->end)
		return;
	if(ext % PAGE)
		ext += PAGE - (ext % PAGE);

	void* old = ctx->end;
	void* brk = sys_brk(old + ext);

	if(brk_error(old, brk))
		fail("brk", NULL, 0);

	ctx->end = brk;
}

static void* alloc(CTX, int len)
{
	extend(ctx, len);
	char* ret = ctx->ptr;
	ctx->ptr += len;
	return ret;
}

static void read_whole(DCT)
{
	struct top* ctx = dct->top;
	char* name = dct->name ? dct->name : ".";
	int fd = dct->fd;
	int ret;

	while((ret = sys_getdents(fd, ctx->ptr, ctx->end - ctx->ptr)) > 0) {
		ctx->ptr += ret;
		extend(ctx, PAGE/2);
	} if(ret < 0)
		fail("getdents", name, ret);
}

static int delength(void* p)
{
	return ((struct dirent*)p)->reclen;
}

static struct dirent** index_dirents(CTX, void* dents, void* deend)
{
	void* p;
	int nument = 0;

	for(p = dents; p < deend; p += delength(p))
		nument++;

	int len = (nument+1) * sizeof(void*);
	struct dirent** idx = alloc(ctx, len);
	struct dirent** end = idx + len;
	struct dirent** ptr = idx;

	for(p = dents; p < deend; p += delength(p)) {
		if(ptr >= end)
			break;
		*(ptr++) = (struct dirent*) p;
	}

	*ptr = NULL;
	
	return idx;
}

static void stat_indexed(DCT, struct dirent** idx)
{
	struct top* ctx = dct->top;
	int opts = ctx->opts;
	int at = dct->fd;

	struct dirent** p;
	struct stat st;

	int flags1 = AT_NO_AUTOMOUNT;
	int flags2 = AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW;

	if(!(opts & SET_stat))
		return;

	for(p = idx; *p; p++) {
		struct dirent* de = *p;
		int type = de->type;

		if(type != DT_UNKNOWN)
			;
		else if(sys_fstatat(at, de->name, &st, flags1) < 0)
			continue;
		else if(S_ISDIR(st.mode))
			type = DT_DIR;
		else if(S_ISLNK(st.mode))
			type = DT_LNK;
		else
			type = DT_REG; /* neither DIR nor LNK nor UNKNOWN */

		de->type = type;

		if(type != DT_LNK)
			continue;
		if(opts & OPT_y)
			continue;

		if(sys_fstatat(at, de->name, &st, flags2) < 0)
			continue;
		if(S_ISLNK(st.mode))
			de->type = DT_LNK_DIR;
	}
}

static int isdirtype(int t)
{
	return (t == DT_DIR || t == DT_LNK_DIR);
}

static int cmpidx(const void* a, const void* b, long opts)
{
	struct dirent* pa = *((struct dirent**)a);
	struct dirent* pb = *((struct dirent**)b);

	if(!(opts & OPT_u)) {
		int dira = isdirtype(pa->type);
		int dirb = isdirtype(pb->type);

		if(dira && !dirb)
			return -1;
		if(dirb && !dira)
			return  1;
	}

	return strcmp(pa->name, pb->name);
}

static void sort_indexed(DCT, struct dirent** idx)
{
	int opts = dct->top->opts;
	struct dirent** p;
	int count = 0;

	for(p = idx; *p; p++)
		count++;

	qsortx(idx, count, sizeof(void*), cmpidx, opts);
}

static void list(DCT);

static void enter_directory(DCT, const char* name, int strict)
{
	int at = dct->fd;
	char* dir = dct->name;
	int dlen = dir ? strlen(dir) : 0;
	int fd;

	FMTBUF(p, e, path, dlen + strlen(name) + 2);
	p = fmtstr(p, e, dir ? dir : "");
	p = fmtstr(p, e, dir ? "/" : "");
	p = fmtstr(p, e, name);
	FMTEND(p, e);

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0) {
		if(strict)
			warn(NULL, path, fd);
		return;
	}

	struct dir sub = {
		.top = dct->top,
		.fd = fd,
		.name = path
	};

	list(&sub);

	sys_close(fd);
}

static void dump_entry(DCT, struct dirent* de)
{
	struct top* ctx = dct->top;
	struct bufout* bo = &(ctx->bo);
	char* name = de->name;
	char type = de->type;

	if(dct->name) {
		bufout(bo, dct->name, strlen(dct->name));
		bufout(bo, "//", 1);
	}

	bufout(bo, name, strlen(name));

	if(type == DT_LNK_DIR)
		bufout(bo, "//", 2);
	else if(type == DT_DIR)
		bufout(bo, "//", 1);

	bufout(bo, "\n", 1);
}

static void dump_indexed(DCT, struct dirent** idx)
{
	int opts = dct->top->opts;
	struct dirent** p;

	for(p = idx; *p; p++) {
		struct dirent* de = *p;
		char* name = de->name;
		char type = de->type;

		if(*name == '.' && !(opts & OPT_a))
			continue;
		if(dotddot(name))
			continue;

		if(type == DT_DIR && (opts & OPT_e))
			;
		else
			dump_entry(dct, de);

		if(!(opts & OPT_r))
			continue;
		if(type == DT_DIR)
			enter_directory(dct, name, MUSTBEDIR);
		else if(type == DT_LNK_DIR && (opts & OPT_w))
			enter_directory(dct, name, MUSTBEDIR);
		else if(type == DT_UNKNOWN)
			enter_directory(dct, name, MAYBEDIR);
	}
}

static void list(DCT)
{
	struct top* ctx = dct->top;

	void* oldptr = ctx->ptr; /* start heap frame */

	void* dents = ctx->ptr;
	read_whole(dct);
	void* deend = ctx->ptr;

	struct dirent** idx = index_dirents(ctx, dents, deend);

	stat_indexed(dct, idx);
	sort_indexed(dct, idx);

	dump_indexed(dct, idx);

	ctx->ptr = oldptr; /* end heap frame */
}

static void list_(CTX, char* openname, char* listname)
{
	int fd;
	int opts = ctx->opts;
	int flags = O_DIRECTORY;

	if(!(opts & OPT_w))
		flags |= O_NOFOLLOW;

	if((fd = sys_open(openname, flags)) < 0)
		fail(NULL, openname, fd);

	struct dir dct = {
		.top = ctx,
		.fd = fd,
		.name = listname
	};

	list(&dct);

	sys_close(fd);
}

static void list_dir(CTX, char* name)
{
	int opts = ctx->opts;
	char* listname;

	int nlen = strlen(name);
	char list[nlen+1];

	memcpy(list, name, nlen+1);

	if(nlen && list[nlen-1] == '/')
		list[nlen-1] = '\0';

	if(opts & OPT_b)
		listname = NULL;
	else
		listname = list;

	list_(ctx, name, listname);
}

static void list_cwd(CTX)
{
	list_(ctx, ".", NULL);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int opts = 0;
	int i = 1;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	init(ctx, opts);

	if(i >= argc)
		list_cwd(ctx);
	else for(; i < argc; i++)
		list_dir(ctx, argv[i]);

	fini(ctx);

	return 0;
}
