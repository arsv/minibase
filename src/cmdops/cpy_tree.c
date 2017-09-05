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

static int movemove(CCT)
{
	return cct->top->move;
}

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

static int open_directory(int at, char* dir, char* name)
{
	int fd;

	if((fd = sys_openat(at, name, O_DIRECTORY)) < 0)
		failat(NULL, dir, name, fd);

	return fd;
}

static int open_creat_dir(int at, char* dir, char* name, int mode)
{
	int ret;

	if((ret = sys_openat(at, name, O_DIRECTORY)) >= 0)
		return ret;
	else if(ret != -ENOENT)
		goto err;
	if((ret = sys_mkdirat(at, name, mode)) < 0)
		goto err;
	if((ret = sys_openat(at, name, O_DIRECTORY)) >= 0)
		return ret;
err:
	failat(NULL, dir, name, ret);
}

static void directory(CCT, char* dstname, char* srcname, struct stat* st)
{
	int move = movemove(cct);

	if(move && rename(cct, dstname, srcname))
		return;

	int srcat = cct->src.at;
	int dstat = cct->dst.at;

	char* srcdir = cct->src.dir;
	char* dstdir = cct->dst.dir;

	int srcfd = open_directory(srcat, srcdir, srcname);
	int dstfd = open_creat_dir(dstat, dstdir, dstname, st->mode);

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

	sys_close(srcfd);
	sys_close(dstfd);

	if(move) rmdir(srcat, srcdir, srcname);
}

static void open_atfd(struct atfd* ff, struct stat* st, int flags, int mode)
{
	int fd, ret;
	int at = ff->at;
	char* dir = ff->dir;
	char* name = ff->name;

	int stf = AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW;

	if((fd = sys_openat4(at, name, flags, mode)) < 0)
		failat(NULL, dir, name, fd);
	if((ret = sys_fstatat(fd, "", st, stf)) < 0)
		failat("stat", dir, name, ret);

	ff->fd = fd;
}

static void prep_file_pair(DST, SRC, uint64_t* size)
{
	struct stat srcst;
	struct stat dstst;

	int sflags = O_RDONLY;
	int dflags = O_WRONLY | O_CREAT | O_TRUNC;

	open_atfd(src, &srcst, sflags, 0);
	open_atfd(dst, &dstst, dflags, srcst.mode);

	if(srcst.dev == dstst.dev && srcst.ino == dstst.ino)
		failat("copy into self:", src->dir, src->name, 0);

	*size = srcst.size;
}

static void copydata(CCT, char* dstname, char* srcname)
{
	struct atfd src = {
		.at = cct->src.at,
		.dir = cct->src.dir,
		.name = srcname
	};

	struct atfd dst = {
		.at = cct->dst.at,
		.dir = cct->dst.dir,
		.name = dstname
	};

	uint64_t size;

	prep_file_pair(&dst, &src, &size);

	transfer(cct, &dst, &src, &size);

	sys_close(dst.fd);
	sys_close(src.fd);
}

static void regular(CCT, char* dstname, char* srcname)
{
	int move = movemove(cct);

	if(move && rename(cct, dstname, srcname))
		return;

	copydata(cct, dstname, srcname);

	if(move) delete(cct, srcname);
}

static void symlink(CCT, char* dstname, char* srcname, struct stat* srcst)
{
	int move = movemove(cct);

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
	if(ret != -EEXIST)
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
		case DT_REG: return regular(cct, dstname, srcname);
		case DT_LNK: return symlink(cct, dstname, srcname, &st);
	}

	warnat("ignoring", srcdir, srcname, 0);
}
