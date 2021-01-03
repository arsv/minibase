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

void check_pac_ext(char* name)
{
	check_extension(name, ".pac");
}

void check_list_ext(char* name)
{
	check_extension(name, ".list");
}

static void parse_header_size(CTX, byte tag[8])
{
	if(memcmp(tag, "PAC", 3))
		fail("not a PAC file", NULL, 0);

	byte c4 = tag[3];

	if((c4 & ~3) != '@')
		fail("not a PAC file", NULL, 0);

	uint size;
	int n = c4 & 3;
	byte* sz = tag + 4;

	size = sz[0];

	if(n > 0)
		size |= (sz[1] << 8);
	if(n > 1)
		size |= (sz[2] << 16);
	if(n > 2) /* header size over 16MB, yikes */
		fail("4-byte index size", NULL, 0);

	uint start = 4 + n + 1;

	ctx->hoff = start;
	ctx->hlen = size;
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
	byte tag[8];
	int ret, fd = ctx->fd;

	if((ret = sys_read(fd, tag, sizeof(tag))) < 0)
		fail("read", NULL, ret);
	if(ret < sizeof(tag))
		fail("package index too short", NULL, 0);

	parse_header_size(ctx, tag);

	uint hoff = ctx->hoff;
	uint hlen = ctx->hlen;

	if(hoff > 8 || hoff + hlen < 8)
		fail("malformed package", NULL, 0);

	uint got = sizeof(tag) - ctx->hoff;
	uint need = hlen - got;
	uint full = hoff + hlen;

	byte* head = heap_alloc(ctx, (full + 3) & ~3);
	byte* rest = head + sizeof(tag);

	memcpy(head, tag, sizeof(tag));

	if((ret = sys_read(fd, rest, need)) < 0)
		fail("read", NULL, ret);
	if(ret < (int)need)
		fail("incomplete read", NULL, 0);

	ctx->head = head;
	ctx->iptr = head + hoff;
	ctx->iend = head + hoff + hlen;
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
