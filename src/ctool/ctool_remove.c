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

	int proto = PROT_READ;
	int flags = MAP_PRIVATE;

	void* buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

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

		if((fd > 0) && ((ret = sys_close(fd)) < 0))
			warn("close", NULL, ret);

		int at = ctx->pfds[i];
		char* name = ctx->path[i];

		if((at == 0) || (i == 0))
			;
		else if((ret = sys_unlinkat(at, name, AT_REMOVEDIR)) >= 0)
			;
		else if((ret != -ENOENT) && (ret != -ENOTEMPTY))
			warn("unlink", name, ret);

		ctx->at = at;
		ctx->ptr = name;
	}

	ctx->depth = lvl;
}

static char* enter_skip_dir(CTX, char* p, char* e)
{
	int lvl = 0;
	int ret;

	while(p < e) {
		char* ds = p;
		char* de = strecbrk(p, e, '/');
		int dl = de - ds;

		if(de >= e) break;

		p = de + 1;

		if(lvl >= ctx->depth) {
			;
		} else if(!strncmp(ctx->path[lvl], ds, dl)) {
			lvl++;
			continue;
		} else {
			backtrack_path(ctx, lvl);
		}

		int at = ctx->at;

		char* tmp = alloc_align(ctx, dl + 1);
		memcpy(tmp, ds, dl);
		tmp[dl] = '\0';

		if(at == 0)
			ret = at;
		else if((ret = sys_openat(at, tmp, O_DIRECTORY)) >= 0)
			;
		else if(ret == -ENOENT)
			ret = 0;
		else
			fail("open", tmp, ret);

		ctx->pfds[lvl] = at;
		ctx->at = ret;
		ctx->path[lvl] = tmp;
		ctx->depth = lvl + 1;
	}

	return p;
}

static void unlink_tail(CTX, char* p, char* e)
{
	int ret, at = ctx->at;
	void* old = ctx->ptr;
	int len = e - p;

	if(!at) return;

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

static void remove_package(CTX, char* name)
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

	prep_filedb_str(ctx, name);

	load_filedb(ctx);

	unlink_files(ctx);

	unlink_filedb(ctx);
}

static void remove_all_packages(CTX)
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

			remove_package(ctx, name);

			heap_reset(ctx, ref);
		}
	} if(ret < 0) {
		fail("getdents", NULL, ret);
	}

	if((ret = sys_close(fd)) < 0)
		fail("close", NULL, ret);
}

void cmd_remove(CTX)
{
	char* name = shift(ctx);

	no_more_arguments(ctx);

	check_workdir(ctx);

	prep_filedb_str(ctx, name);

	load_filedb(ctx);

	unlink_files(ctx);

	unlink_filedb(ctx);
}

void cmd_reset(CTX)
{
	no_more_arguments(ctx);

	check_workdir(ctx);

	remove_all_packages(ctx);
}
