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

/* Tree-walking and direntry-level stuff. */

static void runrec(CCT, char* dname, char* sname, int type);
static void dryrec(CCT, char* dname, char* sname, int type);
static void dryerr(CCT, struct atf* dd, int ret);

static int pathlen(struct atf* dd)
{
	char* name = dd->name;
	char* dir = dd->dir;
	int len = 0;

	if(name)
		len += strlen(name);
	if(dir && dir[0] != '/')
		len += strlen(dir) + 1;

	return len + 1;
}

static void makepath(char* buf, int size, struct atf* dd)
{
	char* p = buf;
	char* e = buf + size - 1;
	char* dir = dd->dir;
	char* name = dd->name;

	if(dir && dir[0] != '/') {
		p = fmtstr(p, e, dir);
		p = fmtstr(p, e, "/");
	} if(name) {
		p = fmtstr(p, e, name);
	}

	*p = '\0';
}

void warnat(const char* msg, struct atf* dd, int err)
{
	char path[pathlen(dd)];

	makepath(path, sizeof(path), dd);

	warn(msg, path, err);
}

void failat(const char* msg, struct atf* dd, int err)
{
	warnat(msg, dd, err);
	_exit(-1);
}

static void set_new_file(struct atf* dd, char* name)
{
	dd->name = name;

	if(dd->fd >= 0)
		sys_close(dd->fd);

	dd->fd = -1;
}

static void start_file_pair(CCT, char* dstname, char* srcname)
{
	set_new_file(&cct->dst, dstname);
	set_new_file(&cct->src, srcname);
	memzero(&cct->st, sizeof(cct->st));
}

void trychown(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;
	int ret;

	if(!cct->top->user)
		return;

	int uid = st->uid;
	int gid = st->gid;

	if(uid == cct->top->uid && gid == cct->top->gid)
		return;
	if((ret = sys_fchown(dst->fd, uid, gid)) < 0)
		failat("chown", dst, ret);

	/* todo: utimens */
}

/* Utils for move/rename mode */

static int rename(CCT)
{
	struct atf* src = &cct->src;
	struct atf* dst = &cct->dst;
	int ret;

	if(!(ret = sys_renameat2(AT(src), AT(dst), 0)))
		return 1;
	if(ret == -EXDEV)
		return 0;

	failat("rename", src, ret);
}

static void delete(CCT)
{
	struct atf* src = &cct->src;
	int ret;

	if((ret = sys_unlinkat(AT(src), 0)) < 0)
		failat("unlink", src, ret);
}

static void rmdir(CCT)
{
	struct atf* src = &cct->src;
	int ret;

	if((ret = sys_unlinkat(AT(src), AT_REMOVEDIR)) < 0)
		failat("rmdir", src, ret);
}

static int writable(int at, char* name)
{
	int flags = AT_EACCESS | AT_SYMLINK_NOFOLLOW;

	return sys_faccessat(at, ".", W_OK | X_OK, flags);
}

static void check_writable_at(CCT)
{
	int ret;
	struct atf* dst = &cct->dst;

	if(cct->wrchecked)
		return;

	cct->wrchecked = 1;

	if((ret = writable(AT(dst))) >= 0)
		return;

	/* Report the at-dir here, not the file the dir is being checked for. */
	char* name = dst->name;

	dst->name = NULL;
	dryerr(cct, dst, ret);
	dst->name = name;
}

/* Tree walking routines */

static void scan_directory(CCT)
{
	int dryrun = cct->top->dryrun;
	int rd, fd = cct->src.at;
	char buf[1024];

	void (*rec)(CCT, char*, char*, int);

	if(dryrun)
		rec = dryrec;
	else
		rec = runrec;

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		char* p = buf;
		char* e = buf + rd;

		while(p < e) {
			struct dirent* de = (struct dirent*) p;

			p += de->reclen;

			if(dotddot(de->name))
				continue;

			rec(cct, de->name, de->name, de->type);
		}
	}
}

static void open_src_dir(CCT)
{
	struct atf* src = &cct->src;
	struct stat* st = &cct->st;
	int fd, ret;

	if((fd = sys_openat(AT(src), O_DIRECTORY)) < 0)
		failat(NULL, src, fd);

	if(cct->top->dryrun)
		; /* no mkdir during dryrun, no need to re-stat it */
	else if((ret = sys_fstat(fd, st)) < 0)
		failat("stat", src, ret);

	src->fd = fd;
}

static int open_dst_dir(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;
	int dryrun = cct->top->dryrun;

	struct stat ds;
	int fd, ret;

	if((fd = ret = sys_openat(dst->at, dst->name, O_DIRECTORY)) == -ENOENT)
		goto make;
	else if(ret < 0)
		goto fail;
	else if(cct->top->newc)
		failat(NULL, dst, -EEXIST);

	if((ret = sys_fstat(fd, &ds)) < 0)
		goto fail;

	if(ds.dev == st->dev && ds.ino == st->ino)
		return -1;

	goto done;

make:
	if(dryrun) {
		check_writable_at(cct);
		return -1;
	}

	if((ret = sys_mkdirat(dst->at, dst->name, st->mode)) < 0)
		goto fail;
	if((fd = ret = sys_openat(dst->at, dst->name, O_DIRECTORY)) < 0)
		goto fail;

done:
	dst->fd = fd;
	trychown(cct);

	return 0;
fail:
	failat(NULL, dst, ret);
}

static void directory(CCT)
{
	struct atf* src = &cct->src;
	struct atf* dst = &cct->dst;
	int move = cct->top->move;

	if(move && rename(cct))
		return;

	open_src_dir(cct);

	if(open_dst_dir(cct))
		return;

	char spath[pathlen(src)];
	char dpath[pathlen(dst)];

	makepath(spath, sizeof(spath), src);
	makepath(dpath, sizeof(dpath), dst);

	struct cct next = {
		.top = cct->top,
		.src = { src->fd, spath, NULL, -1 },
		.dst = { dst->fd, dpath, NULL, -1 }
	};

	scan_directory(&next);

	if(move) rmdir(cct);
}

static void regular(CCT)
{
	int move = cct->top->move;

	if(move && rename(cct))
		return;

	copyfile(cct);

	if(move) delete(cct);
}

/* Symlinks are "copied" as symlinks, retaining the contents.
   Current code does not attempt to translate them from src to dst,
   although at some point it may be nice to have. */

static void symlink(CCT)
{
	struct atf* src = &cct->src;
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;

	if(st->size <= 0 || st->size > 4096)
		failat(NULL, src, -EINVAL);

	long len = st->size + 5;
	char buf[len];
	int ret;

	if((ret = sys_readlinkat(src->at, src->name, buf, len)) < 0)
		failat("readlink", src, ret);

	buf[ret] = '\0';

	if((ret = sys_symlinkat(buf, dst->at, dst->name)) >= 0)
		goto got;
	if(ret != -EEXIST || cct->top->newc)
		goto err;
	if((ret = sys_unlinkat(dst->at, dst->name, 0)) < 0)
		failat("unlink", dst, ret);
	if((ret = sys_symlinkat(buf, dst->at, dst->name)) >= 0)
		goto got;
err:
	failat("symlink", dst, ret);
got:
	delete(cct);
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

void runrec(CCT, char* dname, char* sname, int type)
{
	struct atf* src = &cct->src;
	struct stat* st = &cct->st;
	int flags = AT_SYMLINK_NOFOLLOW;
	int ret;

	start_file_pair(cct, dname, sname);

	if(type == DT_REG)
		;
	else if((ret = sys_fstatat(src->at, src->name, st, flags)) < 0)
		failat(NULL, src, ret);
	else
		type = stifmt_to_dt(st);

	switch(type) {
		case DT_DIR: return directory(cct);
		case DT_REG: return regular(cct);
		case DT_LNK: return symlink(cct);
	}
}

/* Dry run routines */

static void dryerr(CCT, struct atf* dd, int ret)
{
	warnat(NULL, dd, ret);

	if(cct->top->errors++ < 10) return;

	fail("too many errors", NULL, 0);
}

static int stat_check_dir(struct atf* dd, struct stat* st)
{
	int flags = AT_SYMLINK_NOFOLLOW;
	int ret;

	if((ret = sys_fstatat(AT(dd), st, flags)) < 0)
		return ret;

	return ((st->mode & S_IFMT) == S_IFDIR ? 1 : 0);
}

void dryrec(CCT, char* dname, char* sname, int type)
{
	struct atf* src = &cct->src;
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;
	struct stat ds;
	int sdir, ddir;

	start_file_pair(cct, dname, sname);

	if((sdir = stat_check_dir(src, st)) < 0)
		return dryerr(cct, src, sdir);
	if((ddir = stat_check_dir(dst, &ds)) < 0)
		return;

	if(sdir && !ddir)
		return dryerr(cct, dst, -ENOTDIR);
	if(!sdir && ddir)
		return dryerr(cct, dst, -EISDIR);

	if(sdir)
		directory(cct);
	else
		check_writable_at(cct);
}

void dryrun(CCT, char* dname, char* sname, int type)
{
	dryrec(cct, dname, sname, type);
}

void run(CTX, CCT, char* dst, char* src)
{
	int type = DT_UNKNOWN;

	if(ctx->dryrun)
		dryrun(cct, dst, src, type);
	else
		runrec(cct, dst, src, type);
}
