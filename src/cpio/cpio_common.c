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

	ctx->heap.brk = brk;
	ctx->heap.ptr = brk;
	ctx->heap.end = end;
}

void* heap_alloc(CTX, int size)
{
	void* ptr = ctx->heap.ptr;
	void* end = ctx->heap.end;
	void* new = ptr + size;

	if(new > end) {
		void* tmp = sys_brk(ptr + pagealign(size));

		if(brk_error(end, tmp))
			fail("brk", NULL, -ENOMEM);

		ctx->heap.end = end = tmp;
	}

	ctx->heap.ptr = new;

	return ptr;
}

void heap_extend(CTX, int size)
{
	(void)heap_alloc(ctx, size);
}

void heap_reset(CTX, void* ptr)
{
	ctx->heap.ptr = ptr;
}

/* Just a convenience counterpart for heap_reset when reset is done
   to current state without allocating anything. */

void* heap_point(CTX)
{
	return ctx->heap.ptr;
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

	ctx->cpio.fd = fd;
	ctx->cpio.name = name;
}

void make_cpio_file(CTX, char* name)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = 0644;

	check_cpio_ext(name);

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	ctx->cpio.fd = fd;
	ctx->cpio.name = name;
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
	p = fmtu64(p, e, ctx->cpio.off);
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

	int len = dlen + nlen + 15;
	char* buf = alloca(len);

	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, "/");
	p = fmtstr(p, e, name);
	*p++ = '\0';

	fail(NULL, buf, ret);
}

void reset_entry(CTX)
{
	memzero(&ctx->entry, sizeof(ctx->entry));
}

static void format_size(char dst[8], uint val)
{
	char* p = dst;
	char* e = dst + 8;

	for(int i = 24; i >= 0; i -= 8)
		p = fmtbyte(p, e, (val >> i) & 0xFF);
}

static void header_nlen(struct header* hdr, uint len)
{
	format_size(hdr->namesize, len);
}

static void header_mode(struct header* hdr, uint mode)
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
	int ofd = ctx->cpio.fd;
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

static struct header* start_header(CTX, uint extra)
{
	if(ctx->htmp.buf)
		fail("stuck header", NULL, 0);

	struct header* hdr;

	uint skip = ctx->skip;
	uint size = sizeof(*hdr) + extra;
	uint need = skip + size;

	void* buf = heap_alloc(ctx, need);
	uint used = skip + sizeof(*hdr);

	hdr = buf + skip;

	memzero(buf, used);

	ctx->htmp.buf = buf;
	ctx->htmp.ptr = buf + used;
	ctx->htmp.end = buf + need;
	ctx->htmp.size = size;

	memset(hdr, '0', sizeof(*hdr));
	memcpy(hdr->magic, "070701", 6);

	return hdr;
}

static void* grab_header_area(CTX, uint size)
{
	void* dst = ctx->htmp.ptr;
	void* end = ctx->htmp.end;
	void* new = dst + size;

	if(new > end)
		fail("header overflow", NULL, 0);

	ctx->htmp.ptr = new;

	return dst;
}

static void append_header(CTX, void* ptr, uint size)
{
	void* dst = grab_header_area(ctx, size);

	memcpy(dst, ptr, size);
}

static void append_strend(CTX)
{
	char* dst = ctx->htmp.ptr;
	char* end = ctx->htmp.end;

	if(dst >= end)
		fail("header overflow", NULL, 0);

       *dst = '\0';

	ctx->htmp.ptr = dst + 1;
}

static void align_header(CTX)
{
	char* ref = ctx->htmp.buf + ctx->skip;
	char* ptr = ctx->htmp.ptr;
	char* end = ctx->htmp.end;

	uint diff = ptr - ref;
	char* new = ref + align4(diff);

	if(new > end)
		fail("header overflow", NULL, 0);

	for(char* p = ptr; p < new; p++)
		*p = '\0';

	ctx->htmp.ptr = new;
}

static uint aligned_namesize(uint raw)
{
	uint hdrsize = sizeof(struct header); /* = 2 (mod 4) */

	return align4(hdrsize + raw) - hdrsize;
}

static struct header* entry_header(CTX, uint blen)
{
	uint plen = ctx->plen;
	uint nlen = ctx->entry.nlen;
	uint flen = plen + nlen + 1;
	uint extra = aligned_namesize(flen) + align4(blen);

	struct header* hdr = start_header(ctx, extra);

	header_nlen(hdr, flen);

	append_header(ctx, ctx->pref, ctx->plen);
	append_header(ctx, ctx->entry.name, ctx->entry.nlen);
	append_strend(ctx);
	align_header(ctx);

	return hdr;
}

static struct header* named_header(CTX, char* name, uint nlen)
{
	uint flen = nlen + 1;
	uint extra = aligned_namesize(flen);

	struct header* hdr = start_header(ctx, extra);

	header_nlen(hdr, flen);

	append_header(ctx, name, nlen);
	append_strend(ctx);
	align_header(ctx);

	return hdr;
}

static void write_header(CTX, uint skip)
{
	int ret, fd = ctx->cpio.fd;
	void* buf = ctx->htmp.buf;
	uint len = ctx->htmp.end - ctx->htmp.buf;

	if(ctx->htmp.ptr < ctx->htmp.end)
		warn("header underfilled", NULL, 0);

	if((ret = sys_write(fd, buf, len)) < 0)
		fail("write", NULL, ret);
	if(ret != len)
		fail("incomplete write", NULL, 0);

	ctx->skip = skip;

	heap_reset(ctx, buf);

	ctx->htmp.buf = NULL;
	ctx->htmp.ptr = NULL;
	ctx->htmp.end = NULL;
	ctx->htmp.size = 0;
}

void put_pref(CTX)
{
	struct header* hdr;

	hdr = named_header(ctx, ctx->pref, ctx->plen);

	header_mode(hdr, S_IFDIR | 0755);
	header_size(hdr, 0);

	write_header(ctx, 0);
}

void put_file(CTX, uint mode)
{
	int fd, at = ctx->at;

	uint size = ctx->entry.size;
	char* path = ctx->entry.path;

	struct header* hdr = entry_header(ctx, 0);

	header_mode(hdr, S_IFREG | mode);
	header_size(hdr, size);

	write_header(ctx, align4(size) - size);

	if((fd = sys_openat(at, path, O_RDONLY)) < 0)
		fail(NULL, path, fd);

	stream_body(ctx, fd, size);
}

void put_symlink(CTX)
{
	int ret, at = ctx->at;
	uint size = ctx->entry.size;

	struct header* hdr = entry_header(ctx, size);

	header_mode(hdr, S_IFLNK | 0644);
	header_size(hdr, size);

	char* path = ctx->entry.path;
	void* content = grab_header_area(ctx, size);

	if((ret = sys_readlinkat(at, path, content, size)) < 0)
		fail(NULL, path, ret);
	if(ret != (int)size)
		fail("incomplete read:", path, 0);

	align_header(ctx);

	write_header(ctx, 0);
}

void put_immlink(CTX)
{
	uint plen = ctx->entry.plen;
	char* path = ctx->entry.path;

	struct header* hdr = entry_header(ctx, plen);

	header_mode(hdr, S_IFLNK | 0644);
	header_size(hdr, plen);

	append_header(ctx, path, plen);
	align_header(ctx);

	write_header(ctx, 0);
}

void put_trailer(CTX)
{
	char* name = "TRAILER!!!";
	int nlen = strlen(name);

	struct header* hdr = named_header(ctx, name, nlen);

	(void)hdr;

	write_header(ctx, 0);
}
