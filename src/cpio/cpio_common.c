#include <sys/file.h>
#include <sys/mman.h>
#include <sys/fpath.h>
#include <sys/splice.h>

#include <format.h>
#include <printf.h>
#include <string.h>
#include <util.h>

#include "cpio.h"

void heap_init(CTX, int size)
{
	int need = pagealign(size);

	void* brk = sys_brk(NULL);
	void* end = sys_brk(brk + need);

	if(brk_error(brk, end))
		fail("brk", NULL, -ENOMEM);

	ctx->brk = brk;
	ctx->ptr = brk;
	ctx->end = end;
}

void* heap_alloc(CTX, int size)
{
	void* ptr = ctx->ptr;
	void* end = ctx->end;
	void* new = ptr + size;

	if(new > end) {
		void* tmp = sys_brk(ptr + pagealign(size));

		if(brk_error(end, tmp))
			fail("brk", NULL, -ENOMEM);

		ctx->end = end = tmp;
	}

	ctx->ptr = new;

	return ptr;
}

void heap_extend(CTX, int size)
{
	(void)heap_alloc(ctx, size);
}

void heap_reset(CTX, void* ptr)
{
	ctx->ptr = ptr;
}

static void check_extension(char* name, char* suff)
{
	int nlen = strlen(name);
	int slen = strlen(suff);

	if(nlen <= slen)
		;
	else if(!strncmp(name + nlen - slen, suff, 4))
		return;

	FMTBUF(p, e, msg, 100);
	p = fmtstr(p, e, "not a ");
	p = fmtstr(p, e, suff);
	p = fmtstr(p, e, " file:");
	FMTEND(p, e);

	fail(msg, name, 0);
}

void check_cpio_ext(char* name)
{
	check_extension(name, ".cpio");
}

void check_list_ext(char* name)
{
	check_extension(name, ".list");
}

void open_cpio_file(CTX, char* name)
{
	int fd;

	check_cpio_ext(name);

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	ctx->fd = fd;
}

void make_cpio_file(CTX, char* name)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;

	check_cpio_ext(name);

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	ctx->fd = fd;
}

void open_base_dir(CTX, char* name)
{
	int fd;

	if((fd = sys_open(name, O_DIRECTORY)) < 0)
		fail(NULL, name, fd);

	ctx->at = fd;
	ctx->dir = name;
}

void make_base_dir(CTX, char* name)
{
	int ret, fd;

	if((ret = sys_mkdir(name, 0755)) >= 0)
		;
	else if(ret == -EEXIST)
		;
	else
		fail(NULL, name, ret);

	if((fd = sys_open(name, O_DIRECTORY | O_PATH)) < 0)
		fail(NULL, name, fd);

	ctx->at = fd;
	ctx->dir = name;
}

noreturn void fatal(CTX, char* msg)
{
	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, msg);
	p = fmtstr(p, e, " at ");
	p = fmtu64(p, e, ctx->off);
	FMTEND(p, e);

	fail(buf, NULL, 0);
}

noreturn void failx(CTX, char* name, int ret)
{
	char* dir = ctx->dir;
	int dlen = dir ? strlen(dir) : 0;

	char* pref = ctx->pref;
	int plen = ctx->plen;

	int nlen = strlen(name);

	int len = dlen + plen + nlen + 2;
	char* buf = alloca(len);

	char* p = buf;
	char* e = buf + len - 1;

	if(dir) {
		p = fmtstr(p, e, dir);
		p = fmtstr(p, e, "/");
	} if(pref) {
		p = fmtstr(p, e, pref);
	}

	p = fmtstr(p, e, name);

	*p = '\0';

	fail(NULL, buf, ret);
}

noreturn void failz(CTX, char* name, int ret)
{
	char* dir = ctx->dir;

	if(!dir) fail(NULL, name, ret);

	int dlen = strlen(dir);
	int nlen = strlen(name);

	int len = dlen + nlen + 2;
	char* buf = alloca(len);

	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);

	*p = '\0';

	fail(NULL, buf, ret);
}

/* Routines for building individual record headers */

static int aligned_header(int nlen)
{
	return align4(sizeof(struct header) + nlen + 1);
}

static void format_size(char dst[8], uint val)
{
	char* p = dst;
	char* e = dst + 8;

	for(int i = 24; i >= 0; i -= 8)
		p = fmtbyte(p, e, (val >> i) & 0xFF);
}

static void header_magic(struct header* hdr)
{
	memset(hdr, '0', sizeof(*hdr));
	memcpy(hdr->magic, "070701", 6);

	memcpy(hdr->ino, "00000001", 8);
	memcpy(hdr->min, "00000001", 8);
}

static void header_pref(struct header* hdr, int plen, char* pref)
{
	memcpy(hdr->name, pref, plen);
}

static void header_name(struct header* hdr, int plen, int nlen, char* name)
{
	format_size(hdr->namesize, plen + nlen + 1);
	memcpy(hdr->name + plen, name, nlen);
	hdr->name[plen+nlen] = '\0';
}

static void header_mode(struct header* hdr, int mode)
{
	format_size(hdr->mode, mode);
}

static void header_size(struct header* hdr, uint size)
{
	format_size(hdr->filesize, size);
}

static void stream_body(CTX, int fd, uint size)
{
	int ifd = fd;
	int ofd = ctx->fd;
	int ret;

	while(size > 0) {
		int sfb = (size < (1U<<30)) ? size : (1U<<30);

		if((ret = sys_sendfile(ofd, ifd, NULL, sfb)) < 0)
			fail("sendfile", NULL, ret);
		else if(ret == 0)
			break;

		size -= ret;
	} if(size > 0) {
		fail("incomplete read", NULL, 0);
	}
}

static void write_header(CTX, void* buf, int len)
{
	int ret, fd = ctx->fd;

	if((ret = sys_write(fd, buf, len)) < 0)
		fail("write", NULL, ret);
	if(ret != len)
		fail("incomplete write", NULL, 0);
}

void put_pref(CTX)
{
	char* pref = ctx->pref;
	int plen = ctx->plen;
	int skip = ctx->skip;
	int hdrsize = aligned_header(plen);

	int len = skip + hdrsize;
	void* buf = alloca(len);
	struct header* hdr = buf + skip;

	if(skip) memzero(buf, skip);

	header_magic(hdr);
	header_pref(hdr, plen, pref);
	header_name(hdr, plen, 0, "");
	header_mode(hdr, S_IFDIR | 0755);

	write_header(ctx, buf, len);

	ctx->skip = 0;
}

void put_file(CTX, char* path, char* name, uint size, int mode)
{
	int fd, at = ctx->at;
	char* pref = ctx->pref;
	int nlen = strlen(name);
	int plen = ctx->plen;
	int skip = ctx->skip;

	uint namesize = plen + nlen;
	int hdrsize = aligned_header(namesize);

	int len = skip + hdrsize;
	void* buf = alloca(len);
	struct header* hdr = buf + skip;

	if(skip) memzero(buf, skip);

	header_magic(hdr);
	header_pref(hdr, plen, pref);
	header_name(hdr, plen, nlen, name);
	header_mode(hdr, S_IFREG | mode);
	header_size(hdr, size);

	if((fd = sys_openat(at, path, O_RDONLY)) < 0)
		fail(NULL, path, fd);

	write_header(ctx, buf, len);

	stream_body(ctx, fd, size);

	ctx->skip = align4(size) - size;
}

void put_link(CTX, char* path, char* name, uint size)
{
	int ret, at = ctx->at;
	char* pref = ctx->pref;
	int nlen = strlen(name);
	int plen = ctx->plen;
	int skip = ctx->skip;

	int namesize = plen + nlen;
	int hdrsize = aligned_header(namesize);

	int len = skip + hdrsize + size;
	void* buf = alloca(len);
	struct header* hdr = buf + skip;
	void* content = buf + skip + hdrsize;

	if(skip) memzero(buf, skip);

	header_magic(hdr);
	header_size(hdr, size);
	header_pref(hdr, plen, pref);
	header_name(hdr, plen, nlen, name);
	header_mode(hdr, S_IFLNK | 0644);

	if((ret = sys_readlinkat(at, path, content, size)) < 0)
		fail(NULL, path, ret);
	if(ret != size)
		fail("incomplete read:", path, 0);

	write_header(ctx, buf, len);

	ctx->skip = align4(size) - size;
}

void put_immlink(CTX, char* name, int nlen, char* target, int size)
{
	char* pref = ctx->pref;
	int plen = ctx->plen;
	int skip = ctx->skip;

	int namesize = plen + nlen;
	int hdrsize = aligned_header(namesize);

	int len = skip + hdrsize + size;
	void* buf = alloca(len);
	struct header* hdr = buf + skip;
	void* content = buf + skip + hdrsize;

	if(skip) memzero(buf, skip);

	header_magic(hdr);
	header_size(hdr, size);
	header_pref(hdr, plen, pref);
	header_name(hdr, plen, nlen, name);
	header_mode(hdr, S_IFLNK | 0644);

	memcpy(content, target, size);

	write_header(ctx, buf, len);

	ctx->skip = align4(size) - size;
}

void put_trailer(CTX)
{
	char* name = "TRAILER!!!";
	int nlen = strlen(name);
	int skip = ctx->skip;

	int len = skip + aligned_header(nlen);
	void* buf = alloca(len);
	struct header* hdr = buf + skip;

	header_magic(hdr);
	header_name(hdr, 0, nlen, name);

	if(skip) memzero(buf, skip);

	write_header(ctx, buf, len);

	ctx->skip = 0;
}
