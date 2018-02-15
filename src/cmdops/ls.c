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

#define OPTS "auy"
#define OPT_a (1<<0)	/* show all files, including hidden ones */
#define OPT_u (1<<1)	/* uniform listing, dirs and filex intermixed */
#define OPT_y (1<<2)	/* list symlinks as files, regardless of target */

#define SET_stat (1<<16)

struct ent {
	int type;
	int mode;
	int uid;
	int gid;
	uint64_t size;
	unsigned namelen;
	char name[];
};

struct top {
	int opts;
	struct bufout bo;
	int fd;

	int npatt;
	char** patt;

	void* base;
	void* ptr;
	void* end;

	struct ent** idx;
};

#define CTX struct top* ctx

char output[PAGE];

static void init_context(CTX, int opts)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + PAGE);
	int fd;

	if(brk_error(brk, end))
		fail("cannot initialize heap", NULL, 0);

	ctx->opts = opts;

	ctx->base = brk;
	ctx->end = end;
	ctx->ptr = brk;

	ctx->bo.fd = 1;
	ctx->bo.buf = output;
	ctx->bo.len = sizeof(output);
	ctx->bo.ptr = 0;

	if((fd = sys_open(".", O_DIRECTORY)) < 0)
		fail("open", ".", fd);

	ctx->fd = fd;
}

static void fini_context(CTX)
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

static void stat_entry(CTX, int at, struct ent* en)
{
	char* name = en->name;
	int flags = AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW;
	struct stat st;

	if(ctx->opts & SET_stat)
		;
	else if(en->type == DT_UNKNOWN)
		;
	else return;

	if((sys_fstatat(at, name, &st, flags)) < 0)
		return;

	en->mode = st.mode;
	en->size = st.size;
	en->uid = st.uid;
	en->gid = st.gid;

}

static void stat_target(CTX, int at, struct ent* en)
{
	char* name = en->name;
	int flags = AT_NO_AUTOMOUNT;
	struct stat st;

	if(en->type != DT_LNK)
		return;
	if(sys_fstatat(at, name, &st, flags) < 0)
		return;
	if(S_ISDIR(st.mode))
		en->type = DT_LNK_DIR;
}

static void add_dirent(CTX, int at, struct dirent* de)
{
	char* name = de->name;
	int len = strlen(name);
	struct ent* en = alloc(ctx, sizeof(struct ent) + len + 1);

	memzero(en, sizeof(*en));

	en->namelen = len;
	en->type = de->type;
	memcpy(en->name, name, len + 1);

	stat_entry(ctx, at, en);

	stat_target(ctx, at, en);
}

static int match(CTX, char* name)
{
	int all = (ctx->opts & OPT_a);
	int npatt = ctx->npatt;
	char** patt = ctx->patt;

	if(name[0] == '.' && !all)
		return 0;
	if(npatt <= 0)
		return 1;

	for(int i = 0; i < npatt; i++)
		if(strstr(name, patt[i]))
			return 1;

	return 0;
}

static void read_whole(CTX)
{
	char buf[2048];
	int rd, fd = ctx->fd;

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			if(!match(ctx, de->name))
				continue;

			add_dirent(ctx, fd, de);
		}
	} if(rd < 0) {
		fail("getdents", NULL, rd);
	}
}

static int entlen(void* p)
{
	struct ent* en = p;
	return sizeof(*en) + en->namelen + 1;
}

static void index_entries(CTX, void* ents, void* eend)
{
	void* p;
	int nument = 0;

	for(p = ents; p < eend; p += entlen(p))
		nument++;

	int len = (nument+1) * sizeof(void*);
	struct ent** idx = alloc(ctx, len);
	struct ent** end = idx + len;
	struct ent** ptr = idx;

	for(p = ents; p < eend; p += entlen(p)) {
		if(ptr >= end)
			break;
		*(ptr++) = p;
	}

	*ptr = NULL;
	
	ctx->idx = idx;
}

static int isdirtype(int t)
{
	return (t == DT_DIR || t == DT_LNK_DIR);
}

static int cmpidx(const void* a, const void* b, long opts)
{
	struct ent* pa = *((struct ent**)a);
	struct ent* pb = *((struct ent**)b);

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

static void sort_indexed(CTX)
{
	int opts = ctx->opts;
	struct ent** idx = ctx->idx;
	struct ent** p;
	int count = 0;

	for(p = idx; *p; p++)
		count++;

	qsortx(idx, count, sizeof(void*), cmpidx, opts);
}

static void dump_entry(CTX, struct ent* de)
{
	struct bufout* bo = &(ctx->bo);
	char* name = de->name;
	char type = de->type;

	bufout(bo, name, strlen(name));

	if(type == DT_LNK_DIR)
		bufout(bo, "//", 2);
	else if(type == DT_DIR)
		bufout(bo, "//", 1);

	bufout(bo, "\n", 1);
}

static void dump_indexed(CTX)
{
	struct ent** idx = ctx->idx;
	struct ent** p;

	for(p = idx; *p; p++) {
		struct ent* de = *p;
		dump_entry(ctx, de);
	}
}

static void list_directory(CTX)
{
	void* ents = ctx->ptr;
	read_whole(ctx);
	void* eend = ctx->ptr;

	index_entries(ctx, ents, eend);
	sort_indexed(ctx);
	dump_indexed(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int opts = 0, i = 1;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->npatt = argc - i;
	ctx->patt = argv + i;

	init_context(ctx, opts);
	list_directory(ctx);
	fini_context(ctx);

	return 0;
}
