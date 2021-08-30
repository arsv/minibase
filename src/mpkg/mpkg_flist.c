#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/mman.h>

#include <config.h>
#include <format.h>
#include <string.h>
#include <output.h>
#include <util.h>

#include "mpkg.h"

/* During package installation, after processing the .pac index, but before
   unpacking the content, we store the list of files about to be unpacked
   in a pre-defined location, so that we'd be able to remove them later. */

struct subcontext {
	struct top* ctx;
	struct bufout bo;
};

#define SCT struct subcontext* sct

static void open_pkg_file(SCT)
{
	struct top* ctx = sct->ctx;

	char* name = ctx->lstname;
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int mode = 0644;
	int fd;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	struct bufout* bo = &sct->bo;
	int len = 4096;

	bo->fd = fd;
	bo->buf = alloc_align(ctx, len);
	bo->ptr = 0;
	bo->len = len;
}

static void prep_repo_dir(SCT)
{
	struct top* ctx = sct->ctx;
	char* name = ctx->lstname;
	int ret;

	if(!name) /* should not happen */
		return;

	char* base = basename(name);

	if(!ctx->group)
		return;

	if(base == name)
		return; /* very unlikely */

	base--; *base = '\0';

	if((ret = sys_mkdir(name, 0755)) < 0 && ret != -EEXIST)
		fail(NULL, name, ret);

	*base = '/';
}

void check_filedb(CTX)
{
	char* name = ctx->lstname;
	int flags = AT_SYMLINK_NOFOLLOW;
	struct stat st;
	int ret;

	if((ret = sys_fstatat(AT_FDCWD, name, &st, flags)) >= 0)
		fail(NULL, name, -EEXIST);

	if(ret == -ENOTDIR)
		return;
	if(ret == -ENOENT)
		return;

	fail(NULL, name, ret);
}

static void write_str(SCT, char* str, int len)
{
	int ret;

	if((ret = bufout(&sct->bo, str, len)) < 0)
		fail("write", NULL, ret);
}

static void write_part(SCT, char* str, char c)
{
	char buf[1];

	buf[0] = c;

	write_str(sct, str, strlen(str));
	write_str(sct, buf, 1);
}

static void write_line(SCT, struct node* nd)
{
	struct top* ctx = sct->ctx;
	int i, depth = ctx->depth;
	char* prefix = ctx->prefix;

	if(prefix)
		write_part(sct, prefix, '/');

	for(i = 0; i < depth; i++)
		write_part(sct, ctx->path[i], '/');

	write_part(sct, nd->name, '\n');
}

static void write_prefix(SCT)
{
	struct top* ctx = sct->ctx;
	char* prefix = ctx->prefix;

	if(!prefix || !strcmp(prefix, "/"))
		return;

	write_str(sct, prefix, strlen(prefix));
	write_str(sct, "/\n", 2);
}

static void write_entries(SCT)
{
	struct top* ctx = sct->ctx;
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	need_zero_depth(ctx);

	for(; nd < ne; nd++) {
		int bits = nd->bits;
		int depth = ctx->depth;

		if(bits & TAG_DIR) {
			int lvl = bits & TAG_DEPTH;

			if(lvl > depth || lvl > MAXDEPTH)
				fail("invalid pac index", NULL, 0);

			ctx->path[lvl] = nd->name;
			ctx->depth = lvl + 1;
		} else {
			if(!(bits & BIT_NEED))
				continue;

			write_line(sct, nd);
		}
	}

	ctx->depth = 0;
}

static void flush_output(SCT)
{
	int ret;

	if((ret = bufoutflush(&sct->bo)) < 0)
		fail("write", NULL, ret);
}

void write_filedb(CTX)
{
	struct subcontext context, *sct = &context;

	memzero(sct, sizeof(*sct));

	void* ptr = ctx->ptr;

	sct->ctx = ctx;

	prep_repo_dir(sct);
	open_pkg_file(sct);

	write_prefix(sct);
	write_entries(sct);
	flush_output(sct);

	ctx->ptr = ptr;
}
