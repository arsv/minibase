#include <sys/file.h>
#include <sys/mman.h>
#include <sys/creds.h>
#include <sys/signal.h>
#include <string.h>
#include <main.h>
#include <util.h>
#include <lzma.h>

/* Same as ./lunzip.c but using the library LZMA routines */

#define MAX_MAP_SIZE 1*1024*1024

ERRTAG("libunz");

struct top {
	struct lzma* lz;

	uint32_t crc;
	uint32_t crctbl[256];

	int in;
	int mode;
	uint64_t size;
	uint64_t off;
	char* name;

	int out;

	int dictsize;

	uint64_t opos;
	uint64_t ipos;
};

#define CTX struct top* ctx

static void update_crc(CTX, byte* buf, int len)
{
	uint crc = ctx->crc;
	int i, n = len;
	uint32_t* crctbl = ctx->crctbl;

	for(i = 0; i < n; i++) {
		uint idx = (crc ^ buf[i]) & 0xFF;
		crc = crctbl[idx] ^ (crc >> 8);
	}

	ctx->crc = crc;
}

static void write_out(CTX, void* buf, int len)
{
	int ret, fd = ctx->out;

	update_crc(ctx, buf, len);

	if((ret = writeall(fd, buf, len)) < 0)
		fail("write", NULL, ret);

	ctx->opos += len;
}

static void init_crc_table(CTX)
{
	uint i, c, k;
	static const uint mask[2] = { 0x00000000, 0xEDB88320U };

	for(i = 0; i < 256; i++) {
		c = i;

		for(k = 0; k < 8; k++)
			c = (c >> 1) ^ mask[c & 1];

		ctx->crctbl[i] = c;
	}

	ctx->crc = 0xFFFFFFFF;
}

static void open_out_explicit(CTX, char* name)
{
	int fd;
	int flags = O_WRONLY | O_CREAT | O_TRUNC;
	int mode = ctx->mode;

	if((fd = sys_open3(name, flags, mode)) < 0)
		fail(NULL, name, fd);

	ctx->out = fd;
}

static void open_out_desuffix(CTX, char* name)
{
	int nlen = strlen(name);

	if(nlen < 3 || strcmp(name + nlen - 3, ".lz"))
		fail("no .lz suffix:", name, 0);

	char buf[nlen];
	memcpy(buf, name, nlen - 3);
	buf[nlen-3] = '\0';

	return open_out_explicit(ctx, buf);
}

static void open_input(CTX, char* name)
{
	int fd, ret;
	struct stat st;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	ctx->name = name;
	ctx->in = fd;
	ctx->size = st.size;
	ctx->mode = st.mode;
}

static void load_initial_input(CTX)
{
	struct lzma* lz = ctx->lz;
	off_t filesize = ctx->size;
	int fd = ctx->in;
	int size = MAX_MAP_SIZE;
	int ret, hwm;

	if(size > filesize) {
		size = filesize;
		hwm = filesize;
	} else {
		hwm = size - PAGE;
	}

	int proto = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", ctx->name, ret);

	ctx->off = size;

	lz->srcbuf = buf;
	lz->srcptr = buf;
	lz->srchwm = buf + hwm;
	lz->srcend = buf + size;
}

static int uptopage(int len)
{
	return (len + (PAGE-1)) & ~(PAGE-1);
}

static long downtopage(long len)
{
	return (len & ~(PAGE-1));
}

static void prep_initial_output(CTX)
{
	uint dictsize = ctx->dictsize;
	int ret;

	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	uint size = uptopage(dictsize + dictsize) + PAGE;

	if(size >= MAX_MAP_SIZE)
		;
	else if(size < ctx->size)
		size = uptopage(ctx->size) + PAGE;

	byte* buf = sys_mmap(NULL, size, prot, flags, -1, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	struct lzma* lz = ctx->lz;

	lz->dstbuf = buf;
	lz->dstptr = buf;
	lz->dsthwm = buf + size - PAGE;
	lz->dstend = buf + size;
}

static void shift_input_buf(CTX)
{
	struct lzma* lz = ctx->lz;
	int ret;

	void* buf = lz->srcbuf;
	void* ptr = lz->srcptr;
	void* end = lz->srcend;

	long gone = ptr - buf;
	long free = downtopage(gone);
	long len = end - buf;

	if(free > 0) {
		if((ret = sys_munmap(buf, free)) < 0)
			fail("munmap", NULL, ret);

		buf += free;
		len -= free;

		ctx->ipos += free;
	}

	off_t size = ctx->size;
	off_t off = ctx->off;

	if(off >= size)
		fail("input stream unexpected end", NULL, 0);

	int add = MAX_MAP_SIZE;
	int hwm = PAGE;
	void* old = buf;

	if(off + add > size) {
		add = size - off;
		hwm = 0;
	}

	buf = sys_mremap(old, len, len + add, MREMAP_MAYMOVE);

	if((ret = mmap_error(buf)))
		fail("mremap", NULL, ret);

	lz->srcbuf = buf;
	lz->srcptr = buf + (ptr - old);
	lz->srchwm = buf + len + add - hwm;
	lz->srcend = buf + len + add;

	ctx->off += add;
}

static void shift_output_buf(CTX)
{
	struct lzma* lz = ctx->lz;
	int dictsize = ctx->dictsize;
	int ret;

	void* buf = lz->dstbuf;
	void* ptr = lz->dstptr;
	void* end = lz->dstend;

	long ready = ptr - buf;

	if(ready <= dictsize)
		fail("flush below dictsize", NULL, 0);

	long flush = downtopage(ready - dictsize);

	if(flush > 0) {
		write_out(ctx, buf, flush);

		if((ret = sys_munmap(buf, flush)) < 0)
			fail("munmap", NULL, ret);

		buf += flush;
	}

	long len = end - buf;
	long add = flush;
	void* old = buf;
	int hwm = PAGE;

	buf = sys_mremap(old, len, len + add, MREMAP_MAYMOVE);

	if((ret = mmap_error(buf)))
		fail("mremap", NULL, ret);

	lz->dstbuf = buf;
	lz->dstptr = buf + (ptr - old);
	lz->dsthwm = buf + len + add - hwm;
	lz->dstend = buf + len + add;
}

static void check_file_header(CTX)
{
	struct lzma* lz = ctx->lz;
	byte* data = lz->srcptr;

	if(ctx->size < 6)
		fail("not a lzip archive:", ctx->name, 0);
	if(memcmp(lz->srcbuf, "LZIP\x01", 5))
		fail("not a lzip archive:", ctx->name, 0);

	byte dscode = data[5];
	uint dictsize = 1 << (dscode & 0x1F);
	dictsize -= (dictsize/16) * ((dscode >> 5) & 7);

	if(dictsize < (1<<12) || dictsize > (1<<29))
		fail("invalid dictonary size", NULL, 0);

	lz->srcptr = data + 7;
	ctx->dictsize = dictsize;
}

static long mmaped_input_left(CTX)
{
	struct lzma* lz = ctx->lz;

	void* ptr = lz->srcptr;
	void* end = lz->srcend;

	return (end - ptr);
}

static uint32_t get_word(byte* at)
{
	uint32_t ret = 0;

	ret  = at[0];
	ret |= at[1] << 8;
	ret |= at[2] << 16;
	ret |= at[3] << 24;

	return ret;
}

static uint64_t get_long(byte* at)
{
	uint32_t lo = get_word(at + 0);
	uint32_t hi = get_word(at + 4);

	uint64_t ret = hi;

	ret = (ret << 32) | lo;

	return ret;
}

static void check_file_footer(CTX)
{
	struct lzma* lz = ctx->lz;

	if(mmaped_input_left(ctx) < 4 + 2*8)
		shift_input_buf(ctx);

	byte* buf = lz->srcbuf;
	byte* ptr = lz->srcptr;

	uint32_t crc = get_word(ptr); ptr += 4;
	uint64_t osize = get_long(ptr); ptr += 8;
	uint64_t isize = get_long(ptr); ptr += 8;

	ctx->ipos += (ptr - buf);

	uint32_t our = ctx->crc ^ 0xFFFFFFFF;

	if(crc != our)
		fail("wrong CRC", NULL, 0);
	if(osize != ctx->opos)
		fail("file size mismatch", NULL, 0);
	if(isize != ctx->ipos)
		fail("compressed size mismatch", NULL, 0);
	if(isize != ctx->size)
		fail("trailing garbage", NULL, 0);
}

static void flush_output(CTX)
{
	struct lzma* lz = ctx->lz;
	byte* buf = lz->dstbuf;
	byte* ptr = lz->dstptr;
	byte* end = lz->dstend;

	if(ptr <= buf)
		return;
	if(ptr >= end)
		ptr = end;

	long len = ptr - buf;

	write_out(ctx, buf, len);
}

static void report_lzma_error(int ret)
{
	if(ret == LZMA_OUTPUT_OVER)
		fail("LZMA output overflow", NULL, 0);
	if(ret == LZMA_INPUT_OVER)
		fail("LZMA input underrun", NULL, 0);
	if(ret == LZMA_INVALID_REF)
		fail("LZMA invalid reference", NULL, 0);
	if(ret == LZMA_RANGE_CHECK)
		fail("LZMA internal range check failure", NULL, 0);

	fail("LZMA failure code", NULL, ret);
}

static void inflate(CTX)
{
	struct lzma* lz = ctx->lz;
	int ret;

	load_initial_input(ctx);
	check_file_header(ctx);
	prep_initial_output(ctx);
next:
	ret = lzma_inflate(lz);

	if(ret == LZMA_STREAM_END) {
		goto done;
	} else if(ret == LZMA_NEED_INPUT) {
		shift_input_buf(ctx);
		goto next;
	} else if(ret == LZMA_NEED_OUTPUT) {
		shift_output_buf(ctx);
		goto next;
	} else {
		report_lzma_error(ret);
		_exit(0xFF); /* not reached */
	}
done:
	flush_output(ctx);
	check_file_footer(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	byte state[LZMA_SIZE];

	memzero(ctx, sizeof(*ctx));

	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(!(ctx->lz = lzma_create(state, sizeof(state))))
		fail("cannot init LZMA decoder", NULL, 0);

	init_crc_table(ctx);
	open_input(ctx, argv[1]);

	if(argc == 2)
		open_out_desuffix(ctx, argv[1]);
	else if(argc == 3)
		open_out_explicit(ctx, argv[2]);
	else
		fail("too many arguments", NULL, 0);

	inflate(ctx);

	return 0;
}
