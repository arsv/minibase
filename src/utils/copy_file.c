#include <sys/dents.h>
#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/fprop.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <format.h>
#include <memoff.h>
#include <string.h>
#include <util.h>

#include "copy.h"

/* Contents trasfer for regular files. Surprisingly non-trivial task
   in Linux if we want to keep things fast and reliable. */

#define RWBUFSIZE 1024*1024

static int sendfile(CCT, off_t* size)
{
	struct atf* dst = &cct->dst;
	struct atf* src = &cct->src;

	int sfd = src->fd;
	int dfd = dst->fd;

	off_t done = 0;
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

static void readwrite(CCT, off_t* size)
{
	struct atf* dst = &cct->dst;
	struct atf* src = &cct->src;
	struct top* ctx = cct->top;

	if(!ctx->buf)
		alloc_rw_buf(ctx);

	off_t done = 0;

	char* buf = ctx->buf;
	size_t len = ctx->len;

	if(mem_off_cmp(len, *size) > 0)
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
   sendfile fails for one file we stop using it for the whole directory. */

static void moveblock(CCT, off_t* size)
{
	if(cct->nosendfile)
		;
	else if(sendfile(cct, size) >= 0)
		return;
	else
		cct->nosendfile = 1;

	readwrite(cct, size);
}

/* Except for the last line, the code below is only there to deal with sparse
   files. See lseek(2) for explanation. The idea is to seek over the holes and
   only write data-filled blocks.

   Non-sparse files contain one block spanning the whole file and no holes,
   so a single call to moveblock is enough.

   Sparse files are rare, so we try to skip the hole-hopping code as soon
   as it becomes clear there are likely no holes in the source file. */

static void transfer(CCT)
{
	struct stat* st = &cct->st;
	char* rname = cct->src.name;
	char* wname = cct->dst.name;
	int rfd = cct->src.fd;
	int wfd = cct->dst.fd;
	int ret;

	if(512*st->blocks >= st->size)
		goto plain;

	off_t size = st->size;
	off_t ds = 0;
	off_t de;
	off_t blk;

	if(((sys_llseek(rfd, 0, &ds, SEEK_DATA)) < 0) || ds >= size)
		goto plain;
	if(((sys_llseek(rfd, ds, &de, SEEK_HOLE)) < 0) || de >= size)
		goto plain;

	sys_ftruncate(wfd, size);

	while(1) {
		if((ret = sys_seek(wfd, ds)) < 0)
			fail("seek", wname, ret);
		if((ret = sys_seek(rfd, ds)) < 0)
			fail("seek", rname, ret);

		if((blk = de - ds) > 0)
			moveblock(cct, &blk);

		if(de >= size)
			break;

		if((sys_llseek(rfd, de, &ds, SEEK_DATA)) < 0)
			break;
		if((sys_llseek(rfd, ds, &de, SEEK_HOLE)) < 0)
			break;
	}

	return;

plain:
	moveblock(cct, &st->size);
}

/* The source tree is supposed to be static while cpy works. It may no be
   however, so there's a small chance that getdents() reports a regular
   file but subsequent fstat() suddenly shows something non-regular.
   Even if unlikely, it's probably better to check it.

   Same problem arises at the destination, but unlink() there is much less
   sensitive and will happily nuke anything that's not a directory.

   The primary point in calling stat() here is to get the mode (and possibly
   uids/gids and times) to be used later for the new file. Having the size
   is nice but not crucial. */

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

static void open_dst(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;

	int fd;
	int creat = O_WRONLY | O_CREAT | O_EXCL;
	int mode = st->mode;

	if((fd = sys_openat4(AT(dst), creat, mode)) < 0)
		failat(NULL, dst, fd);

	dst->fd = fd;

	trychown(cct);
}

/* Hard-linking for multilink files. This should have been a nice and clean
   piece of code but it turns out linkat(..., AT_EMPTY_PATH) is a *privileged*
   call in Linux for some reason and we have to do it with file names instead.

   The idea here is that src/a and src/b are the same file, then the same
   should hold for dst/a and dst/b, but dst/a should still refer to a copy
   of src/a.

   To achieve this, we remember destination (at,name) and source (dev:ino)
   the first time we copy a file with nlink > 1, and try to link to that
   first copy when we encounter the same source (dev:ino) again.

   Directory handle $at must remain open way long than it would otherwise,
   so we just dup() it and let the original be closed as usual. Keeping the
   original one open would mess up the tree-walking code a lot for no good
   reason. Hard links are presumably rare.

   This wastes one fd for each directory with multi-linked files in it.
   Typical open file limit in Linux is around 1024, so we should be good. */

static struct link* find_link(CCT)
{
	struct top* ctx = cct->top;
	struct stat* st = &cct->st;

	if(!ctx->brk)
		return NULL;

	char* p = ctx->brk;
	char* e = ctx->ptr;

	while(p < e) {
		struct link* ln = (struct link*) p;

		if(!ln->len)
			break; /* wtf */
		if(ln->sdev == st->dev && ln->sino == st->ino)
			return ln;

		p += ln->len;
	}

	return NULL;
}

static int linklen(int slen)
{
	return sizeof(struct link) + slen + 1;
}

static int align4(int len)
{
	return len + (4 - len%4) % 4;
}

static struct link* alloc_link(CCT, int len)
{
	struct top* ctx = cct->top;

	if(!ctx->brk) {
		void* brk = sys_brk(0);
		void* new = sys_brk(brk + PAGE);

		if(brk_error(brk, new))
			return NULL;

		ctx->brk = brk;
		ctx->ptr = brk;
		ctx->end = new;
	}

	void* ptr = ctx->ptr;
	void* req = ptr + len;

	if(req > ctx->end) {
		void* end = ctx->end;
		void* new = sys_brk(end + PAGE);

		if(brk_error(end, new))
			return NULL;

		ctx->end = new;
	}

	ctx->ptr = req;

	return ptr;
}

void note_ino(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;
	struct stat ds;
	struct link* ln;
	int ret;

	char* name = dst->name;
	int slen = strlen(name);
	int alen = align4(linklen(slen));

	if(!(ln = alloc_link(cct, alen)))
		return;
	if((ret = sys_fstat(dst->fd, &ds)) < 0)
		goto drop;
	if(cct->dstatdup)
		ln->at = cct->dstatdup;
	else if((ret = sys_dup(dst->at)) < 0)
		goto drop;
	else
		ln->at = cct->dstatdup = ret;

	ln->sdev = st->dev;
	ln->sino = st->ino;
	ln->ddev = ds.dev;
	ln->dino = ds.ino;

	memcpy(ln->name, name, slen + 1);
	ln->len = alen;

	return;
drop:
	cct->top->ptr = ln;
}

int link_dst(CCT)
{
	struct atf* dst = &cct->dst;
	struct link* ln;
	struct stat ds;
	int ret;

	if(!(ln = find_link(cct)))
		return 0;

	if((ret = sys_unlinkat(AT(dst), 0)) >= 0)
		;
	else if(ret != -ENOENT)
		return 0;

	if((ret = sys_linkat(AT(ln), AT(dst), 0)) < 0)
		return 0;

	/* Check for mis-linking, fall back to copy if it happens.
	   The copy code will unlink dst so need to worry here. */
	if((ret = sys_fstatat(AT(dst), &ds, AT_SYMLINK_NOFOLLOW)) < 0)
		return 0;
	if(ds.dev != ln->ddev || ds.ino != ln->dino)
		return 0;

	return 1;
}

/* Entry point for all the stuff above */

void copyfile(CCT)
{
	struct atf* dst = &cct->dst;
	struct stat* st = &cct->st;

	open_src(cct);

	if(st->nlink < 2)
		;
	else if(link_dst(cct))
		return;

	open_dst(cct);

	if(dst->fd < 0)
		return;
	if(!st->size)
		return;

	transfer(cct);

	if(st->nlink < 2)
		return;

	note_ino(cct);
}
