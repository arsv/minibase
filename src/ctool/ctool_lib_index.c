#include <sys/mman.h>
#include <sys/file.h>

#include <string.h>
#include <printf.h>
#include <util.h>

#include "ctool.h"

/* The code here takes raw .pac index (already loaded in memory) and creates
   an array of `struct node`s ctx->index[] pointing to individual entries in
   the original index. This is done to simplify index traversal later,
   and also to store a bit of additional data for each node */

struct subcontext {
	struct top* ctx;

	void* ptr;

	byte* p;
	byte* e;

	char* last;
	char* prev[MAXDEPTH]; /* saved last values */

	int indirs; /* we're past the leaf nodes within a directory */
};

#define CCT struct subcontext* cct

static void parse_size(CCT, struct node* nd)
{
	byte* p = cct->p;
	byte* e = cct->e;
	int n = nd->bits & TAG_SIZE;
	uint size = 0;

	if(p + n + 1 > e)
		fail("truncated index", NULL, 0);

	size = *p++;

	if(n > 0)
		size |= (*p++) << 8;
	if(n > 1)
		size |= (*p++) << 16;
	if(n > 2)
		size |= (*p++) << 24;

	nd->size = size;
	cct->p = p;
}

static void parse_name(CCT, struct node* nd)
{
	byte* p = cct->p;
	byte* e = cct->e;
	char* name = (char*)p;
	byte* z = p + PAGE;

	if(e > z) e = z; /* no names over 4096 chars long */

	while(p < e && *p)
		p++;
	if(p >= e)
		fail("non-terminated name in index", NULL, 0);

	nd->name = name;
	cct->p = p + 1;
}

static int check_chars(char* name)
{
	byte* p = (byte*)name;
	byte c;

	while((c = *p++))
		if(c < 0x20)
			break;
		else if(c == '/')
			break;

	return c;
}

/* Make sure we aren't unpacking ".." and other weird things.

   Also, enforce strict entry order so that config-driven tree tagging
   would not need to worry about duplicate dir entries later. Currently
   it does simple pick-first-matching traversal, with duplicate entries
   it would need to do much more elaborate search. */

static void validate_name(CCT, struct node* nd)
{
	char* name = nd->name;

	if(!name[0])
		fail("empty node name", NULL, 0);
	if(name[0] == '.')
		fail("invalid node name:", name, 0);

	if(check_chars(name))
		fail("invalid node name:", name, 0);

	char* last = cct->last;

	if(last && strcmp(last, name) >= 0)
		fail("name out of order:", name, 0);

	cct->last = name;
}

/* We store path for error messages and such, we also store the last
   name seen at this depth for the entry order check above. */

static void enter_dir(CCT, struct node* nd)
{
	struct top* ctx = cct->ctx;
	int depth = ctx->depth;

	if(depth >= MAXDEPTH)
		fail("tree depth exceeded", NULL, 0);

	char* name = nd->name;

	ctx->depth = depth + 1;
	ctx->path[depth] = name;
	cct->prev[depth] = name;
	cct->last = NULL;
	cct->indirs = 0;
}

static void rewind_path(CCT, int todepth)
{
	struct top* ctx = cct->ctx;
	int depth = ctx->depth;

	if(!cct->indirs) {
		cct->last = NULL;
		cct->indirs = 1;
	}

	if(todepth > depth)
		fail("invalid index entry", NULL, 0);
	if(todepth == depth)
		return;

	ctx->depth = todepth;
	cct->last = cct->prev[todepth];
}

static struct node* new_node(CCT, int lead)
{
	struct node* nd = alloc_exact(cct->ctx, sizeof(*nd));

	memzero(nd, sizeof(*nd));

	nd->bits = lead;

	return nd;
}

static int take_node(CCT)
{
	byte* p = cct->p;
	byte* e = cct->e;

	if(p >= e)
		return 0;

	int lead = *p++;

	cct->p = p;

	if(lead & TAG_DIR)
		rewind_path(cct, lead & TAG_DEPTH);

	struct node* nd = new_node(cct, lead);

	if(!(lead & TAG_DIR))
		parse_size(cct, nd);

	parse_name(cct, nd);

	validate_name(cct, nd);

	if(lead & TAG_DIR)
		enter_dir(cct, nd);

	return 1;
}

static void init_treeidx(CTX, CCT)
{
	memzero(cct, sizeof(*cct));

	void* p = ctx->head + ctx->hoff;
	void* e = p + ctx->hlen;

	ctx->depth = 0;

	cct->p = p;
	cct->e = e;

	cct->ptr = ctx->ptr;

	cct->ctx = ctx;
}

static void fini_treeidx(CTX, CCT)
{
	struct node* brk = cct->ptr;
	struct node* end = ctx->ptr;
	uint count = end - brk;

	ctx->depth = 0;

	if(!count)
		fail("empty package", NULL, 0);

	ctx->index = brk;
	ctx->nodes = count;
}

static void index_nodes(CTX)
{
	struct subcontext context, *cct = &context;

	init_treeidx(ctx, cct);

	while(take_node(cct))
		;

	fini_treeidx(ctx, cct);
}

static void parse_header_size(CTX, byte tag[8])
{
	if(memcmp(tag, "PAC", 3))
		fail("not a PAC file", NULL, 0);

	int n = tag[3];

	if((n & ~3))
		fail("not a PAC file", NULL, 0);

	uint size;
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

void load_index(CTX)
{
	byte tag[8];
	int ret, fd = ctx->pacfd;

	if((ret = sys_read(fd, tag, sizeof(tag))) < 0)
		fail("read", NULL, ret);
	if(ret < (int)sizeof(tag))
		fail("package index too short", NULL, 0);

	parse_header_size(ctx, tag);

	uint hoff = ctx->hoff;
	uint hlen = ctx->hlen;

	if(hoff > 8 || hoff + hlen < 8)
		fail("malformed package", NULL, 0);

	uint got = sizeof(tag) - ctx->hoff;
	uint need = hlen - got;
	uint full = hoff + hlen;

	byte* head = alloc_align(ctx, full);
	byte* rest = head + sizeof(tag);

	memcpy(head, tag, sizeof(tag));

	if((ret = sys_read(fd, rest, need)) < 0)
		fail("read", NULL, ret);
	if(ret < (int)need)
		fail("incomplete read", NULL, 0);

	ctx->head = head;

	index_nodes(ctx);
}
