#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/dents.h>
#include <sys/mman.h>
#include <sys/splice.h>

#include <string.h>
#include <format.h>
#include <main.h>
#include <util.h>

#include "mpac.h"

void heap_init(CTX, int size)
{
	int need = pagealign(size);

	void* brk = sys_brk(NULL);
	void* end = sys_brk(brk + need);

	if(brk_error(brk, end))
		quit(ctx, "brk", NULL, -ENOMEM);

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
			quit(ctx, "brk", NULL, -ENOMEM);

		ctx->end = end = tmp;
	}

	ctx->ptr = new;

	return ptr;
}

void heap_reset(CTX, void* ptr)
{
	ctx->ptr = ptr;
}

void check_pac_ext(char* name)
{
	int nlen = strlen(name);

	if(nlen <= 4)
		;
	else if(!strncmp(name + nlen - 4, ".pac", 4))
		return;

	fail("no .pac suffix", NULL, 0);
}

static uint parse_header_size(CTX, void* buf, int len)
{
	char* hdr = buf;

	if(len < 4)
		fail("file too short", NULL, 0);
	if(memcmp(hdr, "PAC", 3))
		fail("not a PAC file", NULL, 0);

	byte c4 = *((byte*)&hdr[3]);

	if((c4 & ~3) != '@')
		fail("not a PAC file", NULL, 0);

	uint size;
	int n = c4 & 3;

	if(len < 4 + 1 + n)
		fail("file too short", NULL, 0);

	byte* sz = buf + 4;

	size = sz[0];

	if(n > 0)
		size |= (sz[1] << 8);
	if(n > 1)
		size |= (sz[2] << 16);
	if(n > 2)
		size |= (sz[3] << 24);

	uint start = 4 + n + 1;

	ctx->iptr = buf + start;
	ctx->iend = buf + start + size;

	return start + size;
}

static void parse_size(CTX, byte lead)
{
	byte* ptr = ctx->iptr;
	byte* end = ctx->iend;
	int n = lead & TAG_SIZE;

	uint size = 0;

	if(ptr + n + 1> end)
		quit(ctx, "truncated index", NULL, 0);

	size = *ptr++;

	if(n > 0)
		size |= (*ptr++) << 8;
	if(n > 1)
		size |= (*ptr++) << 16;
	if(n > 2)
		size |= (*ptr++) << 24;

	ctx->size = size;
	ctx->iptr = ptr;
}

static void parse_name(CTX)
{
	byte* ptr = ctx->iptr;
	byte* end = ctx->iend;
	byte* tmp = ptr;

	while(ptr < end && *ptr)
		ptr++;
	if(ptr >= end)
		quit(ctx, "non-terminated name in index", NULL, 0);

	char* name = (char*)tmp;

	if(dotddot(name) || !!strchr(name, '/'))
		quit(ctx, "invalid entry name", name, 0);

	ctx->name = name;
	ctx->nlen = ptr - tmp;
	ctx->iptr = ptr + 1;
}

void load_index(CTX)
{
	int len = PAGE;
	void* buf = heap_alloc(ctx, len);
	int ret, fd = ctx->fd;

	if((ret = sys_read(fd, buf, len)) < 0)
		fail("read", NULL, ret);

	uint got = ret;

	uint total = parse_header_size(ctx, buf, got);

	if(total <= got) {
		ctx->left = got - total;
		ctx->lptr = buf + total;
		return;
	}

	void* rest = buf + got;
	uint need = total - got;

	(void)heap_alloc(ctx, need);

	if((ret = sys_read(fd, rest, need)) < 0)
		fail("read", NULL, ret);
	if(ret < need)
		fail("incomplete read", NULL, 0);
}

void open_pacfile(CTX, char* name)
{
	int fd;

	check_pac_ext(name);

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	ctx->fd = fd;
}

int next_entry(CTX)
{
	byte* ptr = ctx->iptr;
	byte* end = ctx->iend;

	if(ptr >= end)
		return -1;

	byte lead = *ptr;
	ctx->iptr = ptr + 1;

	ctx->name = NULL;
	ctx->nlen = 0;
	ctx->size = 0;

	if(lead & TAG_DIR) {
		parse_name(ctx);
	} else {
		parse_size(ctx, lead);
		parse_name(ctx);
	}

	return lead;
}

void failx(CTX, const char* msg, char* name, int ret)
{
	char* root = ctx->root;
	int i, n = ctx->depth;
	int len = 2;

	len += strlen(root) + 1;

	for(i = 0; i < n; i++)
		len += strlen(ctx->path[i]) + 1;

	len += strlen(name);

	char* buf = alloca(len);

	char* p = buf;
	char* e = buf + len - 1;

	p = fmtstr(p, e, ctx->root);
	p = fmtstr(p, e, "/");

	for(i = 0; i < n; i++) {
		p = fmtstr(p, e, ctx->path[i]);
		p = fmtstr(p, e, "/");
	}

	p = fmtstr(p, e, name);

	*p++ = '\0';

	fail(msg, buf, ret);
}
