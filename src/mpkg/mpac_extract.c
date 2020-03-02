#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <string.h>
#include <main.h>
#include <util.h>

#include "mpac.h"

/* Archive unpacking code. Nothing unexpected here, just go through
   the index, opening or creating directories as necessary.

   When unpacking, this tool works similar to tar, overwriting files
   without aking and using existing directories without warnings. */

static void open_outdir(CTX, char* name)
{
	int fd, ret;

	if((ret = sys_mkdir(name, 0755)) >= 0)
		;
	else if(ret != -EEXIST)
		fail(NULL, name, ret);

	if((fd = sys_open(name, O_PATH)) < 0)
		fail(NULL, name, fd);

	ctx->at = fd;
	ctx->root = name;
}

static char* prep_link(CTX, int size)
{
	char* link = heap_alloc(ctx, size + 1);
	char* dptr = link;
	int ret;

	uint left = ctx->left;
	void* lptr = ctx->lptr;

	if(!left) goto read;

	uint copy = (left < size ? left : size);

	memcpy(dptr, lptr, copy);

	ctx->lptr = lptr + copy;
	ctx->left = left - copy;

	dptr += copy;
	size -= copy;

	if(!size) goto done;
read:
	if((ret = sys_read(ctx->fd, dptr, size)) < 0)
		fail("read", NULL, ret);
	if(ret != size)
		fail("incomplete read", NULL, ret);

	dptr += size;
done:
	*dptr = '\0';

	return link;
}

static void unpack_link(CTX)
{
	char* name = ctx->name;
	uint size = ctx->size;

	void* ptr = ctx->ptr;
	char* link = prep_link(ctx, size);
	int ret, at = ctx->at;

	if((ret = sys_symlinkat(link, at, name)) >= 0)
		goto out;
	if(ret != -EEXIST)
		failx(ctx, NULL, name, ret);
	if((ret = sys_unlinkat(at, name, 0)) < 0)
		failx(ctx, "unlink", name, ret);
	if((ret = sys_symlinkat(link, at, name)) < 0)
		failx(ctx, NULL, name, ret);
out:
	ctx->ptr = ptr;
}

static void transfer_data(CTX, int fd)
{
	uint size = ctx->size;
	uint left = ctx->left;
	void* lptr = ctx->lptr;
	uint write;
	int ret;

	if(!left)
		goto send;
	if(size > left)
		write = left;
	else
		write = size;

	if((ret = sys_write(fd, lptr, write)) < 0)
		fail("write", NULL, ret);
	if(ret != write)
		fail("incomplete write", NULL, 0);

	ctx->left = left - write;
	ctx->lptr = lptr + write;
	size -= write;

	if(!size) return;
send:
	if((ret = sys_sendfile(fd, ctx->fd, NULL, size)) < 0)
		fail("sendfile", NULL, ret);
	if(ret != size)
		fail("incomplete write", NULL, 0);
}

static void unpack_file(CTX, int mode)
{
	char* name = ctx->name;
	int at = ctx->at;
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int fd, ret;

	if((fd = sys_openat4(at, name, flags, mode)) >= 0)
		goto got;
	if(fd != -EEXIST)
		failx(ctx, NULL, name, fd);
	if((ret = sys_unlinkat(at, name, 0)) < 0)
		failx(ctx, "unlink", name, ret);
	if((fd = sys_openat4(at, name, flags, mode)) < 0)
		failx(ctx, NULL, name, ret);
got:
	transfer_data(ctx, fd);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

static void rewind_path(CTX, int todepth)
{
	int depth = ctx->depth;
	int ret;

	if(todepth > depth)
		fail("invalid index entry", NULL, 0);

	while(depth > todepth) {
		if((ret = sys_close(ctx->at)) < 0)
			fail("close", NULL, ret);

		depth--;
		ctx->at = ctx->pfds[depth];
	}

	ctx->depth = depth;
}

static void unpack_dir(CTX, int lead)
{
	rewind_path(ctx, lead & TAG_DEPTH);

	char* name = ctx->name;
	int depth = ctx->depth;
	int at = ctx->at;
	int fd, ret;

	if(depth >= MAXDEPTH)
		fail("tree depth exceeded", NULL, 0);

	if((ret = sys_mkdirat(at, name, 0755)) >= 0)
		;
	else if(ret != -EEXIST)
		failx(ctx, NULL, name, ret);

	if((fd = sys_openat(at, name, O_DIRECTORY|O_PATH)) < 0)
		failx(ctx, NULL, name, ret);

	ctx->path[depth] = name;
	ctx->pfds[depth] = at;
	ctx->at = fd;
	ctx->depth = depth + 1;
}

static void unpack_data(CTX)
{
	int lead;

	while((lead = next_entry(ctx)) >= 0) {
		int type = lead & TAG_TYPE;

		if(lead & TAG_DIR)
			unpack_dir(ctx, lead);
		else if(type == TAG_LINK)
			unpack_link(ctx);
		else if(type == TAG_EXEC)
			unpack_file(ctx, 0755);
		else if(type == TAG_FILE)
			unpack_file(ctx, 0644);
	}

	rewind_path(ctx, 0);
}

void cmd_extract(CTX)
{
	char* infile = shift(ctx);
	char* outdir = shift(ctx);

	no_more_arguments(ctx);

	heap_init(ctx, 2*PAGE);

	open_pacfile(ctx, infile);
	open_outdir(ctx, outdir);
	load_index(ctx);

	unpack_data(ctx);
}
