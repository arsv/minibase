#include <sys/file.h>
#include <sys/dents.h>
#include <string.h>

#include <format.h>
#include <printf.h>
#include <output.h>
#include <util.h>

#include "shell.h"

/* Directory listing. The code builds a stack of entries (struct ent)
   in the heap, indexes them, and dumps the results.

   Some filtering on top to list only files of specific types and/or
   only those with a particular substring in the name. */

#define BUFSIZE 4096
#define MAXENTS 100

#define LS_HIDE_DOTTED (1<<0)
#define LS_HIDE_NONDOT (1<<1)
#define LS_ONLY_DIRS   (1<<2)
#define LS_ONLY_EXEC   (1<<3)
#define LS_ONLY_FILES  (1<<4)

#define DT_LNK_DIR 71	/* symlink pointing to a dir, custom value */

struct ent {
	int type;
	int mode;
	uint64_t size;
	unsigned namelen;
	char name[];
};

struct listcontext {
	int dirfd;
	int flags;

	char* patt;
	int plen;

	struct ent** idx;

	void* buf;
	int size;

	void* p0;
	void* p1;

	int total;
	int matching;
	int indexed;
};

#define CTX struct listcontext* ctx

static void stat_entry(CTX, int at, struct ent* en)
{
	char* name = en->name;
	int flags = AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW;
	struct stat st;

	if((sys_fstatat(at, name, &st, flags)) < 0)
		return;

	en->mode = st.mode;
	en->size = st.size;
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

	/* mark symlinks-to-executable-target as executable */
	en->mode |= st.mode & 0111;
}

static int pattern_match(CTX, char* name)
{
	char* patt = ctx->patt;
	int plen = ctx->plen;

	if(!patt || !plen)
		return 1;

	int nlen = strlen(name);

	if(!(ctx->flags & LS_HIDE_NONDOT)) {
		if(patt[0] == '.') {
			if(nlen <= plen)
				return 0;

			return !memcmp(name + nlen - plen, patt, plen);
		}
	}

	return !strncmp(name, patt, plen);
}

static int pre_match(CTX, struct dirent* de)
{
	int flags = ctx->flags;
	char* name = de->name;
	int type = de->type;

	if(name[0] == '.') {
		if(flags & LS_HIDE_DOTTED)
			return 0;
	} else {
		if(flags & LS_HIDE_NONDOT)
			return 0;
	}

	if(!type) /* don't know yet */
		return 1;

	if(flags & LS_ONLY_DIRS)
		if((type != DT_DIR) && (type != DT_LNK))
			return 0;

	if(flags & LS_ONLY_EXEC)
		if((type != DT_REG) && (type != DT_LNK))
			return 0;

	if(flags & LS_ONLY_FILES)
		if((type == DT_DIR) || (type == DT_LNK))
			return 0;

	if(!pattern_match(ctx, name))
		return 0;

	return 1;
}

static int post_match(CTX, struct ent* en)
{
	int flags = ctx->flags;
	int type = en->type;
	int mode = en->mode;

	if(type == DT_LNK_DIR)
		type = DT_DIR;

	if(flags & LS_ONLY_DIRS) {
		if(type != DT_DIR)
			return 0;
	}

	if(flags & LS_ONLY_EXEC) {
		if(type != DT_REG)
			return 0;
		if(!(mode & 0111))
			return 0;
	}

	return 1;
}

static int align4(int x)
{
	return (x + 3) & ~3;
}

static void add_dirent(CTX, int at, struct dirent* de)
{
	if(!pre_match(ctx, de))
		return;

	char* name = de->name;
	int len = strlen(name);
	int size = align4(sizeof(struct ent) + len + 1);

	struct ent* en = heap_alloc(size);

	memzero(en, sizeof(*en));

	en->namelen = len;
	en->type = de->type;
	memcpy(en->name, name, len + 1);

	stat_entry(ctx, at, en);
	stat_target(ctx, at, en);

	if(!post_match(ctx, en))
		return heap_reset(en); /* undo entry */

	ctx->matching++;

	if(ctx->indexed >= MAXENTS)
		return heap_reset(en);

	ctx->indexed++;
}

static void read_whole(CTX)
{
	int size = BUFSIZE;
	void* buf = ctx->buf;
	int rd, fd = ctx->dirfd;

	ctx->p0 = sh.ptr;

	while((rd = sys_getdents(fd, buf, size)) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;

			ctx->total++;

			add_dirent(ctx, fd, de);
		}
	} if(rd < 0) {
		repl("getdents", NULL, rd);
	}

	ctx->p1 = sh.ptr;
}

static int entlen(void* p)
{
	struct ent* en = p;

	return align4(sizeof(*en) + en->namelen + 1);
}

static void index_entries(CTX)
{
	void* ents = ctx->p0;
	void* eend = ctx->p1;

	void* p;
	int nument = 0;

	for(p = ents; p < eend; p += entlen(p))
		nument++;

	int len = (nument+1) * sizeof(void*);
	struct ent** idx = heap_alloc(len);
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

static int cmpidx(void* a, void* b, long opts)
{
	struct ent* pa = a;
	struct ent* pb = b;

	int dira = isdirtype(pa->type);
	int dirb = isdirtype(pb->type);

	if(dira && !dirb)
		return -1;
	if(dirb && !dira)
		return  1;

	return strcmp(pa->name, pb->name);
}

static void sort_indexed(CTX)
{
	int opts = ctx->flags;
	struct ent** idx = ctx->idx;
	struct ent** p;
	int count = 0;

	for(p = idx; *p; p++)
		count++;

	qsortx(idx, count, cmpidx, opts);
}

static char typechar(struct ent* en)
{
	int type = en->type;

	if(type == DT_DIR)
		return '/';
	if(type == DT_LNK_DIR)
		return '/';
	if(type == DT_LNK)
		return '@';

	if(type == DT_FIFO)
		return '=';
	if((type == DT_CHR) || (type == DT_BLK))
		return '+';

	if(type != DT_REG)
		return '?';

	int mode = en->mode;

	if(mode & 0111)
		return '*';

	return ' ';
}

static void dump_file_name(struct bufout* bo, struct ent* en)
{
	char pref[10];

	char* p = pref;
	char* e = pref + sizeof(pref) - 1;

	p = fmtstr(p, e, " ");
	p = fmtchar(p, e, typechar(en));

	bufout(bo, pref, p - pref);
	bufout(bo, en->name, en->namelen);
	bufout(bo, "\n", 1);
}

static void report_counts(struct bufout* bo, CTX)
{
	char buf[100];

	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, errtag);
	p = fmtstr(p, e, ": ");

	p = fmtstr(p, e, "total ");
	p = fmtint(p, e, ctx->total);

	if(ctx->matching != ctx->total) {
		p = fmtstr(p, e, " matching ");
		p = fmtint(p, e, ctx->matching);
	}

	if(ctx->indexed < ctx->matching) {
		p = fmtstr(p, e, " skipped ");
		p = fmtint(p, e, ctx->matching - ctx->indexed);
	}

	*p++ = '\n';

	bufout(bo, buf, p - buf);
}

static void dump_indexed(CTX)
{
	struct ent** p = ctx->idx;
	struct bufout bo;

	bufoutset(&bo, STDOUT, ctx->buf, BUFSIZE);

	for(; *p; p++) {
		struct ent* de = *p;

		dump_file_name(&bo, de);
	}

	report_counts(&bo, ctx);

	bufoutflush(&bo);
}

static int open_directory(CTX)
{
	int fd;

	if((fd = sys_open(".", O_RDONLY | O_DIRECTORY)) < 0)
		repl("open", ".", fd);

	ctx->dirfd = fd;

	return fd;
}

static void close_directory(CTX)
{
	int ret, fd = ctx->dirfd;

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

void list_common(int flags)
{
	struct listcontext c, *ctx = &c;

	memzero(ctx, sizeof(*ctx));

	ctx->flags = flags;
	ctx->buf = heap_alloc(BUFSIZE);
	ctx->patt = shift();
	ctx->plen = ctx->patt ? strlen(ctx->patt) : 0;

	if(extra_arguments())
		return;

	if(open_directory(ctx) < 0)
		return;

	read_whole(ctx);
	index_entries(ctx);
	sort_indexed(ctx);
	dump_indexed(ctx);

	close_directory(ctx);
}

void cmd_ls(void)
{
	list_common(LS_HIDE_DOTTED);
}

void cmd_la(void)
{
	list_common(0);
}

void cmd_ld(void)
{
	list_common(LS_HIDE_DOTTED | LS_ONLY_DIRS);
}

void cmd_lf(void)
{
	list_common(LS_HIDE_DOTTED | LS_ONLY_FILES);
}

void cmd_lx(void)
{
	list_common(LS_HIDE_DOTTED | LS_ONLY_EXEC);
}

void cmd_lh(void)
{
	list_common(LS_HIDE_NONDOT);
}

void cmd_lhd(void)
{
	list_common(LS_HIDE_NONDOT | LS_ONLY_DIRS);
}

void cmd_lhf(void)
{
	list_common(LS_HIDE_NONDOT | LS_ONLY_FILES);
}
