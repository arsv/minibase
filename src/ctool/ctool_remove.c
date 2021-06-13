#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fpath.h>
#include <sys/dents.h>

#include <output.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "ctool.h"

/* Removal of a library package. This code reads pkg/foo.list, deletes all
   the files mentioned there, and the removes foo.list itself.

   Some extra effort is taken to ensure removal uses atfile calls the same
   way deployment does. Not really necessary, but makes straces easier to
   work with. This whole code could have been just

       for line in foo.list do
           rm $line
       done

   but it's a bit more involved than that. */

#define AT_SKIP 0

static void load_filedb(CTX)
{
	struct stat st;
	char* path = ctx->fdbpath;
	int ret, fd;

	if((fd = sys_open(path, O_RDONLY)) < 0)
		fail(NULL, path, fd);

	ctx->fdbfd = fd;

	if((ret = sys_flock(fd, LOCK_EX)) < 0)
		fail("lock", path, ret);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", path, ret);
	if(st.size > 0xFFFFFFFF)
		fail(NULL, path, -E2BIG);

	uint size = st.size;

	if(!size) goto empty;

	void* buf = alloc_align(ctx, size);

	if((ret = sys_read(fd, buf, size)) < 0)
		fail("read", NULL, 0);
	else if(ret < (int)size)
		fail("incomplete read", NULL, 0);

	ctx->head = buf;
empty:
	ctx->hoff = 0;
	ctx->hlen = size;
}

static void unlink_filedb(CTX)
{
	char* path = ctx->fdbpath;
	int ret;

	if((ret = sys_unlink(path)) < 0)
		fail(NULL, path, ret);
}

static void backtrack_path(CTX, int lvl)
{
	int depth = ctx->depth;
	int ret;

	for(int i = depth - 1; i >= lvl; i--) {
		int fd = ctx->at;

		if(fd == AT_SKIP)
			;
		else if((ret = sys_close(fd)) < 0)
			warn("close", NULL, ret);

		int at = ctx->pfds[i];
		char* name = ctx->path[i];

		if((i == 0) || (at == AT_SKIP))
			;
		else if((ret = sys_unlinkat(at, name, AT_REMOVEDIR)) >= 0)
			;
		else if((ret != -ENOENT) && (ret != -ENOTEMPTY))
			warn("unlink", name, ret);

		ctx->at = at;
	}

	ctx->depth = lvl;
}

/* Given path = [ "inc", "foo" ] and p = "inc/bar/b.h",
   close "foo" to leave [ "inc" ] in path and return "bar/b.h" */

static char* skip_open_dirs(CTX, char* p, char* e)
{
	int depth = ctx->depth;
	int lvl = 0;

	while((p < e) && (lvl < depth)) {
		char* ds = p;
		char* de = strecbrk(p, e, '/');

		if(de >= e)
			break;

		int dl = de - ds;
		char* pi = ctx->path[lvl];
		int pl = strnlen(pi, dl + 1);

		if(pl != dl || memcmp(pi, ds, dl))
			break;

		*de = '\0';
		p = de + 1;
		lvl++;
	}

	backtrack_path(ctx, lvl);

	return p;
}

/* Given path = [ "inc" ] and p = "bar/b.h", open "bar", push it into
   path yielding [ "inc", "bar" ] and return the bare basename, "b.h" */

static char* enter_new_dirs(CTX, char* p, char* e)
{
	int ret;

	while(p < e) {
		char* name = p;
		char* nend = strecbrk(p, e, '/');

		if(nend >= e)
			break;

		p = nend + 1;
		*nend = '\0';

		int at = ctx->at;
		int lvl = ctx->depth;

		if(lvl >= MAXDEPTH)
			fail("max depth exceeded", NULL, 0);

		if(at == AT_SKIP) {
			ret = at;
		} else if((ret = sys_openat(at, name, O_DIRECTORY)) < 0) {
			if(ret != -ENOENT)
				fail("open", name, ret);
			ret = AT_SKIP;
		}

		ctx->pfds[lvl] = at;
		ctx->at = ret;
		ctx->path[lvl] = name;
		ctx->depth = lvl + 1;
	}

	return p;
}

static char* enter_skip_dir(CTX, char* p, char* e)
{
	p = skip_open_dirs(ctx, p, e);

	p = enter_new_dirs(ctx, p, e);

	return p;
}

static void unlink_tail(CTX, char* p, char* e)
{
	int ret, at = ctx->at;
	void* old = ctx->ptr;
	int len = e - p;

	if(at == AT_SKIP)
		return;

	char* tmp = alloc_align(ctx, len + 1);
	memcpy(tmp, p, len);
	tmp[len] = '\0';

	if((ret = sys_unlinkat(at, tmp, 0)) >= 0)
		;
	else if(ret != -ENOENT)
		warn("unlink", tmp, ret);

	ctx->ptr = old;
}

static void unlink_files(CTX)
{
	char* p = ctx->head;
	char* e = p + ctx->hlen;

	need_zero_depth(ctx);

	if(ctx->at == 0)
		fail("initial fdcwd is zero", NULL, 0);

	while(p < e) {
		char* ls = p;
		char* le = strecbrk(p, e, '\n');

		p = le + 1;

		char* bn = enter_skip_dir(ctx, ls, le);

		unlink_tail(ctx, bn, le);
	}

	backtrack_path(ctx, 0);
}

void prep_filedb_str(CTX, char* name)
{
	int nlen = strlen(name);
	int size = nlen + 10;

	if(looks_like_path(name))
		fail("path names not allowed here", NULL, 0);

	char* path = alloc_align(ctx, size);
	char* p = path;
	char* e = path + size - 1;

	p = fmtstr(p, e, "pkg/");
	p = fmtstr(p, e, name);
	p = fmtstr(p, e, ".list");

	*p++ = '\0';

	ctx->fdbpath = path;
}

void remove_package(CTX, char* name)
{
	prep_filedb_str(ctx, name);

	load_filedb(ctx);

	unlink_files(ctx);

	unlink_filedb(ctx);
}

static void remove_entry(CTX, char* name)
{
	char* suff = ".list";
	int slen = strlen(suff);
	int nlen = strlen(name);

	if(nlen < slen)
		return;

	int blen = nlen - slen;

	if(memcmp(name + blen, suff, slen))
		return;

	name[blen] = '\0';

	remove_package(ctx, name);
}

void remove_bindir_files(CTX)
{
	ctx->fdbpath = ".ctool";

	load_filedb(ctx);

	unlink_files(ctx);
}

void remove_all_packages(CTX)
{
	uint size = 4096;
	void* buf = alloc_align(ctx, size);
	void* ref = ctx->ptr;
	char* dir = "pkg";
	int fd, ret;

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	while((ret = sys_getdents(fd, buf, size)) > 0) {
		void* ptr = buf;
		void* end = buf + ret;

		while(ptr < end) {
			struct dirent* de = ptr;
			int reclen = de->reclen;
			char* name = de->name;

			if(!reclen)
				break;

			ptr += reclen;

			if(dotddot(name))
				continue;

			remove_entry(ctx, name);

			heap_reset(ctx, ref);
		}
	} if(ret < 0) {
		fail("getdents", NULL, ret);
	}

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

static void remove_subdir(char* name)
{
	int ret;

	if((ret = sys_unlinkat(AT_FDCWD, name, AT_REMOVEDIR)) >= 0)
		return;
	if(ret == -ENOTEMPTY)
		return;
	if(ret == -ENOENT)
		return;

	warn(NULL, name, ret);
}

static void remove_repo_link(void)
{
	int ret;
	char* name = "rep";

	if((ret = sys_unlinkat(AT_FDCWD, name, 0)) >= 0)
		return;
	if(ret == -ENOENT)
		return;

	warn(NULL, name, ret);
}

void cmd_reset(CTX)
{
	no_more_arguments(ctx);

	check_workdir(ctx);

	remove_all_packages(ctx);

	remove_bindir_files(ctx);

	remove_subdir("bin");
	remove_subdir("lib");
	remove_subdir("inc");
	remove_subdir("obj");
	remove_subdir("pkg");
	remove_repo_link();

	unlink_filedb(ctx);
}
