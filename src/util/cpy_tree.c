#include <sys/dents.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "cpy.h"

/* Tree-walking and direntry-level stuff. */

static void runrec(CCT, char* dname, char* sname, int type);

static int set(CCT, int opt)
{
	return cct->top->opts & opt;
}

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

static void annouce(CCT)
{
	struct atf* dst = &cct->dst;

	char path[pathlen(dst)+1];
	makepath(path, sizeof(path), dst);

	int len = strlen(path);
	path[len] = '\n';

	writeall(STDERR, path, len + 1);
}

static void start_file_pair(CCT, char* dstname, char* srcname)
{
	cct->dst.name = dstname;
	cct->src.name = srcname;

	memzero(&cct->st, sizeof(cct->st));

	if(set(cct, DRY))
		return;
	if(!set(cct, OPT_v))
		return;

	annouce(cct);
}

static void end_file_pair(CCT)
{
	int fd;

	if((fd = cct->dst.fd) >= 0)
		sys_close(fd);
	if((fd = cct->src.fd) >= 0)
		sys_close(fd);

	cct->src.fd = -1;
	cct->dst.fd = -1;

	cct->dst.name = NULL;
	cct->src.name = NULL;
}

void trychown(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;
	int ret;

	if(!set(cct, OPT_u))
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

static int maybe_rename(CCT)
{
	struct atf* src = &cct->src;
	struct atf* dst = &cct->dst;
	int ret;

	if(set(cct, DRY))
		return 0;
	if(!set(cct, OPT_m))
		return 0;
	if(!(ret = sys_renameat2(AT(src), AT(dst), 0)))
		return 1;
	if(ret == -EXDEV)
		return 0;

	failat("rename", src, ret);
}

static void maybe_unlink_src(CCT)
{
	struct atf* src = &cct->src;
	int ret;

	if(!set(cct, OPT_m))
		return;

	if((ret = sys_unlinkat(AT(src), 0)) < 0)
		failat("unlink", src, ret);
}

static void maybe_rmdir_src(CCT)
{
	struct atf* src = &cct->src;
	int ret;

	if(set(cct, DRY))
		return;
	if(!set(cct, OPT_m))
		return;

	if((ret = sys_unlinkat(AT(src), AT_REMOVEDIR)) < 0)
		failat("rmdir", src, ret);
}

/* Dry run errors are reported as warning, up to 10 of them.
   This allows for a nice final message, and gives the user
   a better idea of the real situation.

   Past the dry run, cpy dies on the first error.

   Note dryerr *may* be called with dryrun off (i.e. when
   actually copying stuff). It's way easier to skip to fail()
   here than to mess with reporting at the call point. */

static void dryerr(CCT, struct atf* dd, int ret)
{
	warnat(NULL, dd, ret);

	if(!set(cct, DRY))
		_exit(-1);
	if(set(cct, OPT_q))
		return;
	if(cct->top->errors++ < 10)
		return;

	fail("aborting early due to multiple errors", NULL, 0);
}

/* Dryrun only. We need to know whether we can enter the source
   directories, read the files, and write into the destination dir. */

static int access(int at, char* name, int how)
{
	int flags = AT_EACCESS | AT_SYMLINK_NOFOLLOW;

	return sys_faccessat(at, name, how, flags);
}

static void check_dst_dir(CCT)
{
	struct atf* dst = &cct->dst;
	int ret;

	if(cct->wrchecked)
		return;

	if((ret = access(dst->at, ".", X_OK | W_OK)) >= 0) {
		ret = 1;
	} else {
		/* Report the at-dir here, not the file
		   the dir is being checked for. */
		char* name = dst->name;

		dst->name = NULL;
		dryerr(cct, dst, ret);
		dst->name = name;
	}

	cct->wrchecked = ret;
}

/* The source (always a regular file here) must be readable
   and the destination directory must be writable. */

static void check_src_dst(CCT)
{
	struct atf* src = &cct->src;
	int ret;

	if((ret = access(AT(src), R_OK)) < 0)
		dryerr(cct, src, ret);

	check_dst_dir(cct);
}

/* At the top level, and *only* at the top level, we may get dst=(at, path)
   with path not being a basename. It this case access(at, ".") won't cut it
   so we have to figure out what the real directory is. */

static int check_top_dst(CCT, char* name)
{
	struct atf* dst = &cct->dst;
	char* base = basename(name);

	FMTBUF(p, e, dir, base - name + 4);

	if(name == base)
		p = fmtstr(p, e, ".");
	else
		p = fmtraw(p, e, name, base - name);

	FMTEND(p, e);

	return access(dst->at, dir, X_OK | W_OK);
}

/* Tree walking routines. These are shared between the dry and the real runs,
   and it's leaf calls for specific file types (regular, symlink, directory)
   that have to check for or perform the action. */

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

			if(cct->wrchecked < 0)
				return; /* abort early during dryrun */
		}
	}
}

static int open_src_dir(CCT)
{
	struct atf* src = &cct->src;
	struct stat* st = &cct->st;
	int fd, ret;

	if((ret = fd = sys_openat(AT(src), O_DIRECTORY)) < 0)
		goto fail;

	if(set(cct, DRY))
		; /* no mkdir during dryrun, no need to re-stat it */
	else if((ret = sys_fstat(fd, st)) < 0)
		goto fail;

	src->fd = fd;

	if(!set(cct, DRY))
		;
	else if((ret = access(fd, ".", X_OK | R_OK)) < 0)
		goto fail;

	return 0;
fail:
	dryerr(cct, src, ret);
	return -1;
}

static int open_dst_dir(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;

	struct stat ds;
	int fd, ret;

	if((fd = ret = sys_openat(dst->at, dst->name, O_DIRECTORY)) == -ENOENT)
		goto make;
	else if(ret < 0)
		goto fail;
	if(set(cct, OPT_n)) {
		ret = -EEXIST;
		goto fail;
	}
	if((ret = sys_fstat(fd, &ds)) < 0)
		goto fail;

	if(ds.dev == st->dev && ds.ino == st->ino)
		return -1;

	goto done;
make:
	if(set(cct, DRY)) {
		check_dst_dir(cct);
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
	dryerr(cct, dst, ret);
	return -1; /* skip this directory */
}

static void directory(CCT)
{
	struct atf* src = &cct->src;
	struct atf* dst = &cct->dst;

	if(maybe_rename(cct))
		return;

	if(open_src_dir(cct))
		return;
	if(open_dst_dir(cct))
		return;

	char spath[pathlen(src)];
	char dpath[pathlen(dst)];

	makepath(spath, sizeof(spath), src);
	makepath(dpath, sizeof(dpath), dst);

	struct cct next;
	memzero(&next, sizeof(next));

	next.top = cct->top;
	next.dst.at = dst->fd; next.dst.dir = dpath;
	next.src.at = src->fd; next.src.dir = spath;
	next.src.fd = -1;
	next.dst.fd = -1;

	scan_directory(&next);

	maybe_rmdir_src(cct);
}

static void unlink_dst(CCT)
{
	struct atf* dst = &cct->dst;
	int ret;

	if((ret = sys_unlinkat(AT(dst), 0)) >= 0)
		return;
	if(ret == -ENOENT)
		return;

	failat("unlink", dst, ret);
}

static void regular(CCT)
{
	if(set(cct, DRY))
		return check_src_dst(cct);

	if(maybe_rename(cct))
		return;

	unlink_dst(cct);

	copyfile(cct);

	maybe_unlink_src(cct);
}

/* Symlinks are "copied" as symlinks, retaining the contents.
   Current code does not attempt to translate them from src to dst,
   although at some point it may be nice to have. */

static void symlink(CCT)
{
	struct atf* src = &cct->src;
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;

	if(set(cct, DRY))
		return check_dst_dir(cct);

	if(st->size <= 0 || st->size > 4096)
		failat(NULL, src, -EINVAL);

	long len = st->size + 5;
	char buf[len];
	int ret;

	if((ret = sys_readlinkat(src->at, src->name, buf, len)) < 0)
		failat("readlink", src, ret);

	buf[ret] = '\0';

	unlink_dst(cct);

	if((ret = sys_symlinkat(buf, dst->at, dst->name)) < 0)
		failat("symlink", dst, ret);

	maybe_unlink_src(cct);
}

static void special(CCT)
{
	warnat("ignoring", &cct->src, 0);
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
	int dryrun = set(cct, DRY);
	int flags = AT_SYMLINK_NOFOLLOW;
	int ret;

	start_file_pair(cct, dname, sname);

	if(type == DT_REG || (dryrun && type != DT_UNKNOWN))
		memzero(st, sizeof(*st));
	else if((ret = sys_fstatat(src->at, src->name, st, flags)) < 0)
		failat(NULL, src, ret);
	else
		type = stifmt_to_dt(st);

	switch(type) {
		case DT_DIR: directory(cct); break;
		case DT_REG: regular(cct); break;
		case DT_LNK: symlink(cct); break;
		default: special(cct); break;
	}

	end_file_pair(cct);
}

void run(CCT, char* dname, char* sname)
{
	int type = DT_UNKNOWN;

	if(set(cct, DRY) && check_top_dst(cct, dname))
		return;

	runrec(cct, dname, sname, type);
}
