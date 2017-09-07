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

/* Contents trasfer for regular files. Surprisingly non-trivial task
   in Linux if we want to keep things fast and reliable. */

#define RWBUFSIZE 1024*1024

static int sendfile(CCT, uint64_t* size)
{
	struct atf* dst = &cct->dst;
	struct atf* src = &cct->src;

	int sfd = src->fd;
	int dfd = dst->fd;

	uint64_t done = 0;
	long ret = 0;
	long run = 0x7ffff000;

	if(*size < run)
		run = *size;

	while(1) {
		if(done >= *size)
			break;
		if((ret = sys_sendfile(dfd, sfd, NULL, run)) <= 0)
			break;
		done += ret;
	};

	if(ret >= 0)
		return 0;
	if(!done && ret == -EINVAL)
		return -1;

	failat("sendfile", dst, ret);
}

static void alloc_rw_buf(CTX)
{
	long len = RWBUFSIZE;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_ANONYMOUS;
	char* buf = sys_mmap(NULL, len, prot, flags, -1, 0);

	if(mmap_error(buf))
		fail("mmap", NULL, (long)buf);

	ctx->buf = buf;
	ctx->len = len;
}

static void readwrite(CCT, uint64_t* size)
{
	struct atf* dst = &cct->dst;
	struct atf* src = &cct->src;
	struct top* ctx = cct->top;

	if(!ctx->buf)
		alloc_rw_buf(ctx);

	uint64_t done = 0;

	char* buf = ctx->buf;
	long len = ctx->len;

	if(len > *size)
		len = *size;

	int rd = 0, wr;
	int sfd = src->fd;
	int dfd = dst->fd;

	while(1) {
		if(done >= *size)
			break;
		if((rd = sys_read(sfd, buf, len)) <= 0)
			break;
		if((wr = writeall(dfd, buf, rd)) < 0)
			failat("write", dst, wr);
		done += rd;
	} if(rd < 0) {
		failat("read", src, rd);
	}
}

/* Sendfile may not work on a given pair of descriptors for various reasons.
   If this happens, fall back to read/write calls.

   Generally the reasons depend on directory (and the underlying fs), so if
   sendfile fails for one file stop using it for the whole directory. */

static void moveblock(CCT, uint64_t* size)
{
	if(cct->nosf)
		;
	else if(sendfile(cct, size) >= 0)
		return;
	else
		cct->nosf = 1;

	readwrite(cct, size);
}

/* Except for the last line, the code below is only there to deal with sparse
   files. See lseek(2) for explanation. The idea is to seek over the holes and
   only write data-filled blocks.

   Non-sparse files contain one block spanning the whole file and no holes,
   so a single call to moveblock is enough.

   Sparse files are rare, and it would be really great to skip most of this
   code as fast as possible. Sadly the best we can do is to check the block
   count. */

static void transfer(CCT)
{
	struct stat* st = &cct->st;
	int rfd = cct->src.fd;
	int wfd = cct->dst.fd;

	if(512*st->blocks >= st->size)
		goto plain;

	uint64_t size = st->size;
	int64_t ds;
	int64_t de;
	uint64_t blk;

	ds = sys_lseek(rfd, 0, SEEK_DATA);

	if(ds == -EINVAL || ds >= size)
		goto plain;

	de = sys_lseek(rfd, ds, SEEK_HOLE);

	if(de < 0 || de >= size)
		goto plain;

	sys_ftruncate(wfd, size);

	while(1) {
		sys_lseek(wfd, ds, SEEK_SET);
		sys_lseek(rfd, ds, SEEK_SET);

		if((blk = de - ds) > 0)
			moveblock(cct, &blk);

		if(de >= size)
			break;

		ds = sys_lseek(rfd, de, SEEK_DATA);
		de = sys_lseek(rfd, ds, SEEK_HOLE);

		if(ds < 0 || de < 0)
			break;
	}

	return;

plain:
	moveblock(cct, &st->size);
}

/* The source tree is supposed to be static while cpy works.
   It may no be however, so there's a small chance that getdents()
   reports a regular file but subsequent fstat() suddenly shows
   something non-regular. Even if unlikely, it's probably better
   to check it.

   Same problem arises at the destination, but there unlink() is
   much less sensitive, anything is ok as long as it's not a dir.

   The primary point in calling stat() here is to get file mode
   for the new file. Having its size is nice but not crucial. */

static void open_src(CCT)
{
	struct atf* src = &cct->src;
	struct stat* st = &cct->st;

	int flags = AT_EMPTY_PATH | AT_SYMLINK_NOFOLLOW;
	int fd, ret;

	if((fd = sys_openat(AT(src), O_RDONLY)) < 0)
		failat(NULL, src, fd);
	if((ret = sys_fstatat(fd, "", st, flags)) < 0)
		failat("stat", src, ret);

	src->fd = fd;
}

/* The straight path for the destination is to (try to) unlink whatever's
   there, create a new file, and pipe the data. If the existing entry happens
   to be a dir, then unlink just fails. Then if cpy gets interrupted
   mid-transfer, we lose the old dst file (which we would any, so no big deal)
   but src remains intact.

   This gets much more complicated in case dst == src by whatever means.
   If cpy unlinks it and gets killed mid-transfer, chances are *src* will
   be lost. To mend this, we do an extra lstat call on dst->name to exclude
   that particular case.

   It is probably possible to avoid lstat, but it's going to be tricky
   and risking that because of a single extra syscall hardly makes sense. */

static void open_dst(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;
	struct stat ds;

	int fd, ret;
	int creat = O_WRONLY | O_CREAT | O_EXCL;
	int flags = AT_SYMLINK_NOFOLLOW;
	int mode = st->mode;

	if((ret = sys_fstatat(AT(dst), &ds, flags)) == -ENOENT)
		goto open;
	else if(ret >= 0 && cct->top->newc)
		failat(NULL, dst, ret);
	else if(ret < 0)
		failat("stat", dst, ret);

	if((ds.mode & S_IFMT) == S_IFDIR)
		failat(NULL, dst, -EISDIR);

	if(ds.dev == st->dev && ds.ino == st->ino)
		return; /* same file */

	if((ret = sys_unlinkat(AT(dst), 0)) >= 0)
		;
	else if(ret == -ENOENT)
		;
	else failat("unlink", dst, ret);
open:
	if((fd = sys_openat4(AT(dst), creat, mode)) < 0)
		failat(NULL, dst, fd);

	dst->fd = fd;

	trychown(cct);
}

void copyfile(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;

	open_src(cct);
	open_dst(cct);

	if(dst->fd < 0)
		return;
	if(!st->size)
		return;

	transfer(cct);
}
