#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/mman.h>
#include <sys/dents.h>
#include <sys/splice.h>

#include <string.h>
#include <printf.h>
#include <format.h>
#include <util.h>

#include "cpio.h"

#define EN_DIR 0
#define EN_FILE 1
#define EN_EXEC 2
#define EN_LINK 3

struct ent {
	short len;
	short type;
	uint size;
	char name[];
};

static int cmp_ent(void* pa, void* pb)
{
	struct ent* a = pa;
	struct ent* b = pb;

	int da = (a->type == EN_DIR);
	int db = (b->type == EN_DIR);

	if(!da && db)
		return -1;
	if(da && !db)
		return 1;

	return strcmp(a->name, b->name);
}

static int count_entries(void* p0, void* p1)
{
	void* ptr = p0;
	void* end = p1;
	int count = 0;

	while(ptr < end) {
		struct ent* en = ptr;
		ptr += en->len;
		count++;
	}

	return count;
}

static void scan_directory(CTX);

static void enter_directory(CTX, struct ent* en)
{
	int fd, at = ctx->at;
	char* pref = ctx->pref;
	int depth = ctx->depth;

	if(depth >= MAXDEPTH)
		failx(ctx, en->name, -ELOOP);

	if((fd = sys_openat(at, en->name, O_DIRECTORY)) < 0)
		failx(ctx, en->name, fd);

	int len = strlen(en->name) + 1;

	len += strlen(pref) + 1;

	char* path = alloca(len + 1);
	char* p = path;
	char* e = path + len;

	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, en->name);
	p = fmtstr(p, e, "/");
	*p = '\0';

	ctx->pref = path;
	ctx->plen = strlen(path);
	ctx->at = fd;
	ctx->depth = depth + 1;

	reset_entry(ctx);

	put_pref(ctx);

	scan_directory(ctx);

	ctx->pref = pref;
	ctx->plen = strlen(path);
	ctx->at = at;
	ctx->depth = depth;
}

static void process_entry(CTX, struct ent* en)
{
	int type = en->type;

	char* name = en->name;
	uint nlen = strlen(name);

	ctx->entry.name = name;
	ctx->entry.nlen = nlen;

	ctx->entry.path = name;
	ctx->entry.plen = nlen;

	ctx->entry.size = en->size;

	if(type == EN_DIR)
		enter_directory(ctx, en);
	else if(type == EN_LINK)
		put_symlink(ctx);
	else if(type == EN_EXEC)
		put_file(ctx, 0755 | S_IFREG);
	else
		put_file(ctx, 0644 | S_IFREG);

	reset_entry(ctx);
}

static int index_entries(void* p0, void* p1, int count, struct ent** idx)
{
	void* ptr = p0;
	void* end = p1;
	int i = 0;

	while(ptr < end && i < count) {
		struct ent* en = ptr;
		idx[i++] = en;
		ptr += en->len;
	}

	return i;
}

static void process_dir(CTX, void* p0, void* p1)
{
	int count = count_entries(p0, p1);
	struct ent** idx = heap_alloc(ctx, count*sizeof(void*));

	count = index_entries(p0, p1, count, idx);

	qsortp(idx, count, cmp_ent);

	for(int i = 0; i < count; i++) {
		process_entry(ctx, idx[i]);
	}
}

static void check_dent(CTX, int at, char* name)
{
	struct stat st;
	int flags = AT_SYMLINK_NOFOLLOW;
	int ret;

	if((ret = sys_fstatat(at, name, &st, flags)) < 0)
		failx(ctx, name, ret);

	struct ent* ep;
	int nlen = strlen(name);
	int mode = st.mode;
	int need = sizeof(*ep) + nlen + 1;
	int type = mode & S_IFMT;
	uint size;

	if(type == S_IFDIR) {
		size = 0;
		type = EN_DIR;
	} else if(type == S_IFLNK) {
		if(st.size > 0xFFFF)
			fail(NULL, name, -E2BIG);
		size = st.size;
		type = EN_LINK;
	} else if(type == S_IFREG) {
		if(st.size > 0xFFFFFFFF)
			fail(NULL, name, -E2BIG);
		size = st.size;
		type = (mode & 0111) ? EN_EXEC : EN_FILE;
	} else {
		failx(ctx, name, -EINVAL);
	}

	need = (need + 3) & ~3;

	ep = heap_alloc(ctx, need);

	ep->len = need;
	ep->type = type;
	ep->size = size;

	memcpy(ep->name, name, nlen + 1);
};

static void scan_directory(CTX)
{
	int ret, fd = ctx->at;
	void* buf = ctx->dent.buf;
	int len = ctx->dent.len;

	void* p0 = heap_point(ctx);

	while((ret = sys_getdents(fd, buf, len)) > 0) {
		void* ptr = buf;
		void* end = buf + ret;

		while(ptr < end) {
			struct dirent* de = ptr;

			char* name = de->name;
			int reclen = de->reclen;

			if(reclen <= 0)
				break;

			ptr += reclen;

			if(dotddot(name))
				continue;

			check_dent(ctx, fd, name);
		}
	} if(ret < 0) {
		failx(ctx, "", ret);
	}

	void* p1 = heap_point(ctx);

	process_dir(ctx, p0, p1);

	heap_reset(ctx, p0);
}

void cmd_create(CTX)
{
	char* name = shift(ctx);
	char* dir = shift(ctx);

	no_more_arguments(ctx);

	heap_init(ctx, 4*PAGE);

	ctx->dent.len = 2*PAGE;
	ctx->dent.buf = heap_alloc(ctx, ctx->dent.len);
	ctx->pref = "";
	ctx->plen = 0;

	open_base_dir(ctx, dir);
	make_cpio_file(ctx, name);

	scan_directory(ctx);

	put_trailer(ctx);
}
