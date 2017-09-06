#include <sys/dents.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <util.h>

#include "cpy.h"

/* Tree-walking and direntry-level stuff.

   This tool uses at-functions internally, but needs full paths
   to report errors. */

static int pathlen(char* dir, char* name)
{
	int len = strlen(name);

	if(dir && dir[0] != '/')
		len += strlen(dir) + 1;

	return len + 1;
}

static void makepath(char* buf, int size, char* dir, char* name)
{
	char* p = buf;
	char* e = buf + size - 1;

	if(dir && dir[0] != '/') {
		p = fmtstr(p, e, dir);
		p = fmtstr(p, e, "/");
	}

	p = fmtstr(p, e, name);
	*p = '\0';
}

static void warnat(const char* msg, char* dir, char* name, int err)
{
	int plen = pathlen(dir, name);
	char path[plen];

	makepath(path, sizeof(path), dir, name);

	warn(msg, path, err);
}

void failat(const char* msg, char* dir, char* name, int err)
{
	warnat(msg, dir, name, err);
	_exit(-1);
}

/* Utils for move/rename mode */

static int rename(CCT, char* dstname, char* srcname)
{
	int srcat = cct->src.at;
	int dstat = cct->dst.at;
	char* srcdir = cct->src.dir;
	int ret;

	if((ret = sys_renameat2(srcat, srcname, dstat, dstname, 0)) >= 0)
		return 1;

	if(ret == -EXDEV)
		return 0;

	failat("rename", srcdir, srcname, ret);
}

static void delete(CCT, char* srcname)
{
	int ret;
	int at = cct->src.at;
	char* dir = cct->src.dir;

	if((ret = sys_unlinkat(at, srcname, 0)) < 0)
		failat("unlink", dir, srcname, ret);
}

static void rmdir(int at, char* dir, char* name)
{
	int ret;

	if((ret = sys_unlinkat(at, name, AT_REMOVEDIR)) < 0)
		failat("rmdir", dir, name, ret);
}

/* Tree walking routines */

static void scan_directory(CCT)
{
	int rd, fd = cct->src.at;
	char buf[1024];

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;

			p += de->reclen;

			if(dotddot(de->name))
				continue;

			runrec(cct, de->name, de->name, de->type);
		}
	}
}

static int open_directory(int at, char* dir, char* name, struct stat* st)
{
	int fd, ret;

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		failat(NULL, dir, name, fd);
	if((ret = sys_fstat(fd, st)) < 0)
		failat("stat", dir, name, ret);

	return fd;
}

static int creat_dir(CCT, int at, char* dir, char* name, struct stat* sst)
{
	int fd, ret;
	struct stat st;

	if((fd = ret = sys_openat(at, name, O_DIRECTORY)) == -ENOENT)
		goto make;
	else if(ret < 0)
		goto fail;
	else if(cct->top->newc)
		failat(NULL, dir, name, -EEXIST);

	if((ret = sys_fstat(fd, &st)) < 0)
		goto fail;

	if(st.dev == sst->dev && st.ino == sst->ino)
		return -EALREADY; /* copy into self */

	if(st.mode == sst->mode)
		;
	else if((ret = sys_fchmod(fd, sst->mode)) < 0)
		goto fail;

	return fd;

make:
	if((ret = sys_mkdirat(at, name, sst->mode)) < 0)
		goto fail;
	if((fd = ret = sys_openat(at, name, O_DIRECTORY)) < 0)
		goto fail;

	return fd;

fail:
	failat(NULL, dir, name, ret);
}

static void directory(CCT, char* dstname, char* srcname, struct stat* st)
{
	int move = cct->top->move;

	if(move && rename(cct, dstname, srcname))
		return;

	int srcat = cct->src.at;
	int dstat = cct->dst.at;

	char* srcdir = cct->src.dir;
	char* dstdir = cct->dst.dir;

	int srcfd = open_directory(srcat, srcdir, srcname, st);
	int dstfd = creat_dir(cct, dstat, dstdir, dstname, st);

	if(dstfd == -EALREADY) {
		sys_close(srcfd);
		return;
	}

	int srclen = pathlen(srcdir, srcname);
	int dstlen = pathlen(dstdir, dstname);

	char srcpath[srclen];
	char dstpath[dstlen];

	makepath(srcpath, sizeof(srcpath), srcdir, srcname);
	makepath(dstpath, sizeof(dstpath), dstdir, dstname);

	struct cct next = {
		.top = cct->top,
		.src = { srcfd, srcpath },
		.dst = { dstfd, dstpath }
	};

	scan_directory(&next);

	if(move) rmdir(srcat, srcdir, srcname);

	sys_close(srcfd);
	sys_close(dstfd);
}

static void regular(CCT, char* dstname, char* srcname, struct stat* st)
{
	int move = cct->top->move;

	if(move && rename(cct, dstname, srcname))
		return;

	copyfile(cct, dstname, srcname, st);

	if(move) delete(cct, srcname);
}

/* Symlinks are "copied" as symlinks, retaining the contents.
   Current code does not attempt to translate them from src to dst,
   although at some point it may be nice to have. */

static void symlink(CCT, char* dstname, char* srcname, struct stat* srcst)
{
	int move = cct->top->move;

	int srcat = cct->src.at;
	int dstat = cct->dst.at;

	char* srcdir = cct->src.dir;
	char* dstdir = cct->dst.dir;

	if(srcst->size <= 0 || srcst->size > 4096)
		failat(NULL, srcdir, srcname, -EINVAL);

	long len = srcst->size + 5;
	char buf[len];
	int ret;

	if((ret = sys_readlinkat(srcat, srcname, buf, len)) < 0)
		failat("readlink", srcdir, srcname, ret);

	buf[ret] = '\0';

	if((ret = sys_symlinkat(buf, dstat, dstname)) >= 0)
		goto got;
	if(ret != -EEXIST || cct->top->newc)
		goto err;
	if((ret = sys_unlinkat(dstat, dstname, 0)) < 0)
		failat("unlink", dstdir, dstname, ret);
	if((ret = sys_symlinkat(buf, dstat, dstname)) >= 0)
		goto got;
err:
	failat("symlink", dstdir, dstname, ret);
got:
	if(move) delete(cct, srcname);
}

/* We need to know the type of the source file to decide what to do
   about it, *before* attempting to open it. Normally getdents provides
   enough information, but it may return DT_UNKNOWN and we must be ready
   to stat the file instead to get its type.

   The code tries not to do stat() on regular files which will be opened
   anyway, allowing fstat() on an open fd. */

static int stifmt_to_dt(struct stat* st)
{
	switch(st->mode & S_IFMT) {
		case S_IFREG: return DT_REG;
		case S_IFDIR: return DT_DIR;
		case S_IFLNK: return DT_LNK;
		default: return DT_UNKNOWN;
	}
}

void runrec(CCT, char* dstname, char* srcname, int type)
{
	int srcat = cct->src.at;
	char* srcdir = cct->src.dir;
	int flags = AT_SYMLINK_NOFOLLOW;

	int ret;
	struct stat st;

	if(type == DT_REG)
		memzero(&st, sizeof(st));
	else if((ret = sys_fstatat(srcat, srcname, &st, flags)) < 0)
		failat(NULL, srcdir, srcname, ret);
	else
		type = stifmt_to_dt(&st);

	switch(type) {
		case DT_DIR: return directory(cct, dstname, srcname, &st);
		case DT_REG: return regular(cct, dstname, srcname, &st);
		case DT_LNK: return symlink(cct, dstname, srcname, &st);
	}

	warnat("ignoring", srcdir, srcname, 0);
}
