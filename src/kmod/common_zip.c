#include <sys/file.h>
#include <sys/mman.h>
#include <sys/creds.h>
#include <sys/signal.h>
#include <string.h>
#include <util.h>
#include <lzma.h>

#include "common.h"

/* Built-in support for compresses .lz modules. We take "file.ko.lz" and
   return a mmaped area with decompressed content, without spawning any
   external processes.

   Lzip files include uncompressed size in the footer, we use that to
   pre-allocate output area of the right size. Compresses input files
   are always mmaped whole. */

static uint32_t get_word_at(byte* at)
{
	uint32_t ret = 0;

	ret  = at[0];
	ret |= at[1] << 8;
	ret |= at[2] << 16;
	ret |= at[3] << 24;

	return ret;
}

static uint64_t get_long_at(byte* at)
{
	uint32_t lo = get_word_at(at + 0);
	uint32_t hi = get_word_at(at + 4);

	uint64_t ret = hi;

	ret = (ret << 32) | lo;

	return ret;
}

static uint32_t lzip_crc(struct mbuf* mb)
{
	long len = mb->len;
	byte* buf = mb->buf;
	byte* end = buf + len;

	if(len < 6 + 20)
		return 0;

	return get_word_at(end - 20);
}

static uint32_t calc_crc(struct mbuf* mb)
{
	uint i, c, k;
	static const uint mask[2] = { 0x00000000, 0xEDB88320U };
	uint32_t crc = 0xFFFFFFFF;
	uint32_t crctbl[256];

	for(i = 0; i < 256; i++) {
		c = i;

		for(k = 0; k < 8; k++)
			c = (c >> 1) ^ mask[c & 1];

		crctbl[i] = c;
	}

	byte* ptr = mb->buf;
	byte* end = ptr + mb->len;

	while(ptr < end) {
		uint idx = (crc ^ *ptr++) & 0xFF;
		crc = crctbl[idx] ^ (crc >> 8);
	}

	return crc ^ 0xFFFFFFFF;
}

static void set_lzma_buffers(struct lzma* lz, struct mbuf* raw, struct mbuf* out)
{
	void* buf = raw->buf;
	long len = raw->len;

	void* srcbuf = buf;
	void* srcptr = buf + 7;
	void* srcend = buf + len - 20;

	lz->srcbuf = srcbuf;
	lz->srcptr = srcptr;
	lz->srchwm = srcend;
	lz->srcend = srcend;

	void* dstbuf = out->buf;
	void* dstend = dstbuf + out->len;

	lz->dstbuf = dstbuf;
	lz->dstptr = dstbuf;
	lz->dsthwm = dstend;
	lz->dstend = dstend;
}

static int lzerr(char* msg, char* name)
{
	warn(msg, name, 0);

	return -1;
}

static int report_lzma_error(char* name, int ret)
{
	if(ret == LZMA_OUTPUT_OVER)
		return lzerr("LZMA output overflow in", name);
	if(ret == LZMA_INPUT_OVER)
		return lzerr("LZMA input underrun in", name);
	if(ret == LZMA_INVALID_REF)
		return lzerr("LZMA invalid reference in", name);
	if(ret == LZMA_RANGE_CHECK)
		return lzerr("LZMA range check failure in", name);
	if(ret == LZMA_NEED_INPUT)
		return lzerr("LZMA stream truncated in", name);
	if(ret == LZMA_NEED_OUTPUT)
		return lzerr("LZMA stream overflow in", name);

	warn("LZMA unexpected failure in", name, ret);

	return -1;
}

static int inflate(struct mbuf* raw, struct mbuf* out, char* name)
{
	struct lzma* lz;
	byte lzbuf[LZMA_SIZE];

	if(!(lz = lzma_create(lzbuf, sizeof(lzbuf))))
		fail("LZMA buffer error", NULL, 0);

	set_lzma_buffers(lz, raw, out);

	int ret = lzma_inflate(lz);

	if(ret != LZMA_STREAM_END)
		return report_lzma_error(name, ret);

	if(lz->srcptr != lz->srcend)
		return lzerr("LZMA trailing garbage in", name);
	if(lz->dstptr != lz->dstend)
		return lzerr("LZMA invalid output length in", name);

	if(lzip_crc(raw) != calc_crc(out))
		return lzerr("CRC mismatch in", name);

	return 0;
}

static int alloc_output(struct mbuf* mb, ulong size)
{
	void* buf;
	int ret;
	int prot = PROT_READ | PROT_WRITE;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	int pages = pagealign(size);

	if(mb->buf || mb->full)
		fail("LZMA re-allocating mbuf", NULL, 0);

	buf = sys_mmap(NULL, pages, prot, flags, -1, 0);

	if((ret = mmap_error(buf))) {
		warn("mmap", NULL, ret);
		return ret;
	}

	mb->buf = buf;
	mb->len = size;
	mb->full = pages;

	return 0;
}

static int check_header(struct mbuf* raw, char* name, ulong* size)
{
	ulong len = raw->len;
	byte* buf = raw->buf;

	if(len < 6 + 20)
		return lzerr("archive too short:", name);
	if(memcmp(buf, "LZIP\x01", 5))
		return lzerr("invalid LZIP header in", name);

	byte dscode = buf[5];
	uint dictsize = 1 << (dscode & 0x1F);
	dictsize -= (dictsize/16) * ((dscode >> 5) & 7);

	if(dictsize < (1<<12) || dictsize > (1<<29))
		return lzerr("invalid LZIP dictsize in", name);

	byte* end = buf + len;

	uint64_t isize = get_long_at(end - 8);

	if(isize != len)
		return lzerr("invalid LZIP input size", name);

	uint64_t osize = get_long_at(end - 16);

	*size = osize;

	if(*size != osize)
		return lzerr("LZIP archive too long:", name);

	return 0;
}

int map_lunzip(struct mbuf* mb, char* name)
{
	struct mbuf raw;
	ulong size = 0;
	int ret;

	memzero(&raw, sizeof(raw));

	if((ret = mmap_whole(&raw, name, REQ)) < 0)
		return ret;
	if((ret = check_header(&raw, name, &size)) < 0)
		goto out;
	if((ret = alloc_output(mb, size)) < 0)
		goto out;
	if((ret = inflate(&raw, mb, name)) < 0)
		munmap_buf(mb);
out:
	munmap_buf(&raw);

	return ret;
}
