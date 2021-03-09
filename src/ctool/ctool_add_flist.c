#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>

#include <config.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <util.h>

#include "ctool.h"

/* The code here converts ctx->index into a list of full pathnames
   and saves that as pkg/foo.list for later use in `ctool remove`.
   Basically just `mpac list` redirected into a file.

   Plain list like this is not the easiest format to use for removal
   operations, but it's doable, and it keeps the package metadata
   greppable and human-readable. Which is probably more important in
   ctool than raw performance. */

struct subcontext {
	struct top* top;

	void* buf;
	uint size;

	byte* ptr;
	byte* end;
};

#define CCT struct subcontext* cct

static void create_file_list(CCT)
{
	struct top* ctx = cct->top;
	char* name = ctx->fdbpath;
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int mode = 0644;
	int fd;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	ctx->fdbfd = fd;
}

void check_filedb(CTX)
{
	char* name = ctx->fdbpath;
	int flags = AT_SYMLINK_NOFOLLOW;
	struct stat st;
	int ret;

	if((ret = sys_fstatat(AT_FDCWD, name, &st, flags)) >= 0)
		fail(NULL, name, -EEXIST);

	if(ret == -ENOTDIR)
		return;
	if(ret == -ENOENT)
		return;

	failz(ctx, NULL, name, ret);
}

static void append_file(CTX, CCT, struct node* nd)
{
	void* p = cct->ptr;
	void* e = cct->end;

	int i, depth = ctx->depth;

	for(i = 0; i < depth; i++) {
		p = fmtstr(p, e, ctx->path[i]);
		p = fmtstr(p, e, "/");
	}

	p = fmtstr(p, e, nd->name);
	p = fmtstr(p, e, "\n");

	cct->ptr = p;
}

static uint sum_path_length(int* lens, int lvl)
{
	uint len = 0;

	for(int i = 0; i <= lvl; i++)
		len += lens[i];

	return len;
}

static uint file_list_size(CTX)
{
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	int depth = 0;
	int lens[MAXDEPTH];
	uint plen = 0;
	uint total = 0;

	for(; nd < ne; nd++) {
		int bits = nd->bits;
		int nlen = strlen(nd->name);

		if(bits & TAG_DIR) {
			int lvl = bits & TAG_DEPTH;

			if(lvl > depth)
				break;
			if(lvl >= MAXDEPTH)
				break;

			lens[lvl] = nlen + 1;
			depth = lvl + 1;

			plen = sum_path_length(lens, lvl);
		} else {
			total += plen + nlen + 1;
		}
	} if(nd < ne) {
		fail("malformed index", NULL, 0);
	}

	return total;
}

static void format_entries(CCT)
{
	struct top* ctx = cct->top;
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	if(ctx->depth)
		fail("non-zero initial depth", NULL, 0);

	for(; nd < ne; nd++) {
		int bits = nd->bits;
		int depth = ctx->depth;

		if(bits & TAG_DIR) {
			int lvl = bits & TAG_DEPTH;

			if(lvl > depth)
				break;
			if(lvl >= MAXDEPTH)
				break;

			ctx->path[lvl] = nd->name;
			ctx->depth = lvl + 1;
		} else {
			append_file(ctx, cct, nd);
		}
	} if(nd < ne) {
		fail("malformed index", NULL, 0);
	}

	ctx->depth = 0;
}

static void write_file_list(CCT)
{
	struct top* ctx = cct->top;

	void* buf = cct->buf;
	void* ptr = cct->ptr;
	void* end = cct->end;
	uint len = ptr - buf;

	if(ptr >= end)
		fail("file list area overflow", NULL, 0);

	int fd = ctx->fdbfd;
	char* name = ctx->fdbpath;
	int ret;

	if((ret = sys_write(fd, buf, len)) < 0)
		failz(ctx, "write", name, ret);
	if(ret != (int)len)
		failz(ctx, "write", name, -EINTR);

	sys_close(ctx->fdbfd);

	ctx->fdbfd = -1;
}

static void init_file_list(CCT, CTX)
{
	uint size = file_list_size(ctx) + 1;
	void* buf = alloc_align(ctx, size);

	memzero(cct, sizeof(*cct));

	cct->top = ctx;
	cct->size = size;

	cct->buf = buf;
	cct->size = size;
	cct->end = buf + size;
	cct->ptr = buf;
}

static void fini_file_list(CCT)
{
	heap_reset(cct->top, cct->buf);
}

void write_filedb(CTX)
{
	struct subcontext context, *cct = &context;

	init_file_list(cct, ctx);

	create_file_list(cct);

	format_entries(cct);

	write_file_list(cct);

	fini_file_list(cct);
}
