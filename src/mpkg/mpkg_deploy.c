#include <sys/mman.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/dents.h>
#include <sys/splice.h>

#include <string.h>
#include <format.h>
#include <dirs.h>
#include <main.h>
#include <util.h>

#include "mpkg.h"

/* Deploy procedure overview:

    * parse arguments, locate target root etc
    * read the index index from the .pac file
    * check it against the rule in config file
    * check the index for filesystem conflicts
    * save the list of stuff to be deployed
    * unpack everything

  The overall idea is to maintain undo-ability and every point
  and to fail as early as possible. The stuff that can be checked
  using index only is done first, and filedb is saved before unpacking
  so that if we fail during unpacking we'd still know what to remove.

  The code in this file mostly handled filesystem conflicts, but
  also coordinates the whole sequence. */

static void check_leaf(CTX, struct node* nd)
{
	struct stat st;
	int ret, at = ctx->at;
	char* name = nd->name;
	int flags = AT_SYMLINK_NOFOLLOW;

	if(!(nd->bits & BIT_NEED))
		return;

	if((ret = sys_fstatat(at, name, &st, flags)) >= 0)
		warnx(ctx, NULL, name, -EEXIST);
	else if(ret != -ENOENT)
		warnx(ctx, NULL, name, ret);
}

static void check_dir(CTX, struct node* nd)
{
	int fd, at = ctx->at;
	char* name = nd->name;
	int depth = ctx->depth;

	if(!(nd->bits & BIT_NEED))
		return;
	if(depth >= MAXDEPTH)
		return; /* should not happen at this point */

	if((fd = sys_openat(at, name, O_DIRECTORY | O_PATH)) < 0) {
		if(fd != -ENOENT)
			warnx(ctx, NULL, name, fd);
		return;
	}

	nd->bits |= BIT_EXST;

	ctx->pfds[depth] = at;
	ctx->path[depth] = name;

	ctx->at = fd;
	ctx->depth = depth + 1;
}

static void rewind_path(CTX, int lvl)
{
	int depth = ctx->depth;
	int ret, at = ctx->at;

	while(depth > lvl) {
		depth--;

		if((ret = sys_close(at)) < 0)
			fail("close", NULL, ret);

		at = ctx->pfds[depth];
	}

	ctx->at = at;
	ctx->depth = depth;
}

static void check_conflict(CTX)
{
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	for(; nd < ne; nd++) {
		int bits = nd->bits;

		if(bits & TAG_DIR) {
			int lvl = bits & TAG_DEPTH;

			rewind_path(ctx, lvl);
			check_dir(ctx, nd);
		} else {
			check_leaf(ctx, nd);
		}
	}

	rewind_path(ctx, 0);

	if(ctx->fail) fail("unable to continue", NULL, 0);
}

static void unpack_dir(CTX, struct node* nd)
{
	int at = ctx->at;
	int bits = nd->bits;
	char* name = nd->name;
	int depth = ctx->depth;
	int fd, ret;

	if(!(bits & BIT_NEED))
		return;
	if(depth >= MAXDEPTH) /* should not happen at this point */
		failx(ctx, NULL, name, -ELOOP);

	if((bits & BIT_EXST))
		;
	else if((ret = sys_mkdirat(at, name, 0755)) < 0)
		failx(ctx, NULL, name, ret);

	if((fd = sys_openat(at, name, O_PATH)) < 0)
		failx(ctx, NULL, name, fd);

	ctx->pfds[depth] = at;
	ctx->path[depth] = name;

	ctx->at = fd;
	ctx->depth = depth + 1;
}

static void unpack_back(CTX, int lvl)
{
	int depth = ctx->depth;
	int ret, at = ctx->at;

	while(depth > lvl) {
		depth--;

		if((ret = sys_close(at)) < 0)
			fail("close", NULL, ret);

		at = ctx->pfds[depth];
	}

	ctx->at = at;
	ctx->depth = depth;
}

static void transfer_data(CTX, struct node* nd, int fd)
{
	char* name = nd->name;
	uint size = nd->size;
	uint left = ctx->left;
	void* lptr = ctx->lptr;
	uint write;
	int ret;

	if(!size) return;
	if(!left) goto send;

	if(size > left)
		write = left;
	else
		write = size;

	if((ret = sys_write(fd, lptr, write)) < 0)
		failx(ctx, NULL, name, ret);

	ctx->left = left - write;
	ctx->lptr = lptr + write;
	size -= write;

	if(!size) return;
send:
	if((ret = sys_sendfile(fd, ctx->pacfd, NULL, size)) < 0)
		failx(ctx, NULL, name, ret);
	if(ret != size)
		failx(ctx, NULL, name, -EINTR);
}

static int read_data(CTX, void* buf, uint size)
{
	int fd = ctx->pacfd;
	uint left = ctx->left;
	void* lptr = ctx->lptr;
	uint copy;
	int ret;

	if(!size) return size;
	if(!left) goto read;

	if(size > left)
		copy = left;
	else
		copy = size;

	memcpy(buf, lptr, copy);

	ctx->left = left - copy;
	ctx->lptr = lptr + copy;
	buf += copy;
	size -= copy;

	if(!size) return size;
read:
	if((ret = sys_read(fd, buf, size)) < 0)
		return ret;
	if(ret != size)
		return -EINTR;

	return 0;
}

static int validate_link(char* buf, uint size)
{
	char* p = buf;
	char* e = p + size;

	for(; p < e; p++)
		if(!*p) return -EINVAL;

	return 0;
}

static void unpack_link(CTX, struct node* nd)
{
	int at = ctx->at;
	char* name = nd->name;
	uint size = nd->size;
	int ret;

	if(size > PAGE)
		failx(ctx, NULL, name, -E2BIG);

	char* buf = alloca(size + 1);

	if((ret = read_data(ctx, buf, size)) < 0)
		failx(ctx, NULL, name, ret);

	buf[size] = '\0';

	if((ret = validate_link(buf, size)) < 0)
		failx(ctx, NULL, name, ret);

	if((ret = sys_symlinkat(buf, at, name)) < 0)
		failx(ctx, NULL, name, ret);
}

static void unpack_file(CTX, struct node* nd)
{
	int at = ctx->at;
	char* name = nd->name;
	int flags = O_WRONLY | O_CREAT | O_EXCL;
	int bits = nd->bits;
	int type = bits & TAG_TYPE;
	int mode = (type == TAG_EXEC) ? 0755 : 0644;
	int fd, ret;

	if((fd = sys_openat4(at, name, flags, mode)) < 0)
		failx(ctx, NULL, name, fd);

	transfer_data(ctx, nd, fd);

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

static void unpack_skip(CTX, struct node* nd)
{
	transfer_data(ctx, nd, ctx->nullfd);
}

static void unpack_content(CTX)
{
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	for(; nd < ne; nd++) {
		int bits = nd->bits;
		int depth = ctx->depth;

		if(bits & TAG_DIR) {
			int lvl = bits & TAG_DEPTH;

			ctx->level = lvl;

			if(lvl > depth)
				continue;
			if(lvl < depth)
				unpack_back(ctx, lvl);

			unpack_dir(ctx, nd);
		} else {
			int lvl = ctx->level;
			int type = bits & TAG_TYPE;

			if(lvl > depth)
				unpack_skip(ctx, nd);
			else if(!(bits & BIT_NEED))
				unpack_skip(ctx, nd);
			else if(type == TAG_LINK)
				unpack_link(ctx, nd);
			else
				unpack_file(ctx, nd);
		}
	}

	unpack_back(ctx, 0);
}

/* We only need /dev/null open if there are files of non-zero size
   to be skipped in the tree. If there are none, don't bother opening it.

   Why not open it on demand? Well, much easier to deal with open failure
   if none of the files have been unpacked yet. And it's not that big of
   a problem to open it beforehand. */

static void maybe_open_null(CTX)
{
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	int skip = 0;
	int write = 0;

	for(; nd < ne; nd++) {
		int bits = nd->bits;

		if(bits & TAG_DIR)
			continue;

		if(bits & BIT_NEED)
			write = 1;
		else if(!nd->size)
			continue;
		else
			skip = 1;
	}

	if(!write)
		fail("no files to install", NULL, 0);
	if(!skip)
		return;

	char* name = "/dev/null";
	int fd;

	if((fd = sys_open(name, O_WRONLY)) < 0)
		fail(NULL, name, fd);

	ctx->nullfd = fd;
}

/* Possible invocations:

    deploy [repo:]name archive.pac
    deploy root [repo:]name archive.pac

    So either 2 or 3 arguments. */

void cmd_deploy(CTX)
{
	int n = args_left(ctx);

	setup_root(ctx, n > 2);
	take_package_arg(ctx);
	take_pacfile_arg(ctx);
	no_more_arguments(ctx);

	load_config(ctx);
	load_pacfile(ctx);

	check_filedb(ctx);
	check_config(ctx);
	check_conflict(ctx);
	write_filedb(ctx);

	maybe_open_null(ctx);
	unpack_content(ctx);
}
