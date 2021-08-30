#include <string.h>
#include <util.h>

#include "mpkg.h"

struct configctx {
	struct top* ctx;

	struct node* ns;
	struct node* ne;

	int mark;
	int nopref;
	uint depth;
	struct node* stack[MAXDEPTH];
};

#define CCT struct configctx* cct

static void mark_down(CCT, struct node* nd, int depth)
{
	struct node* ne = cct->ne;
	int mark = cct->mark;

	for(; nd < ne; nd++) {
		int bits = nd->bits;

		if(bits & TAG_DIR) {
			int lvl = bits & TAG_DEPTH;
			if(lvl < depth) return;
		}

		nd->bits = bits | mark;
	}
}

static void mark_glob(CCT, struct node* nd)
{
	mark_down(cct, nd, cct->depth);
}

static void mark_leaf(CCT, struct node* nd)
{
	int bits = nd->bits;

	if(!(cct->mark & BIT_NEED))
		; /* we can skip regular files */
	else if(!(bits | TAG_DIR))
		return; /* we cannot mark regular files */

	nd->bits = bits | cct->mark;

	int depth = bits & TAG_DEPTH;

	mark_down(cct, nd + 1, depth + 1);
}

static void mark_back(CCT)
{
	int i, n = cct->depth;
	int mark = cct->mark;

	for(i = 0; i < n; i++) {
		struct node* nd = cct->stack[i];
		nd->bits |= mark;
	}
}

/* Config line:

        +-- parent node
        v
   /foo/bar/baz/blah
            ^  ^
	    p  e

   Corresponding index structure:

       0 foo
       1 bar
       - somefile        <-- nd = first child of the parent node
       - someotherfile
       2 boo
       - yetanotherfile
       ....
       3 subdir
       ....
       2 baz             <-- the node we need to locate and return
       - onemorefile
       0 nei             <-- end of search, above our target level
       ....

   Note we may need to skip over nodes below our search level.
   We are only interested in nodes that are exactly at cct->depth */


static struct node* locate_node(CCT, struct node* nd, char* p, char* q)
{
	struct node* ne = cct->ne;
	int depth = cct->depth;
	int level = depth;

	char* req_name = p;
	int req_nlen = q - p;

	for(; nd < ne; nd++) {
		int bits = nd->bits;

		if(bits & TAG_DIR) {
			int bits = nd->bits;
			int lvl = bits & TAG_DEPTH;

			if(lvl < depth)
				return NULL;

			level = lvl;
		}

		if(level > depth)
			continue;

		char* name = nd->name;
		int nlen = strlen(name);

		if(nlen != req_nlen)
			continue;
		if(memcmp(name, req_name, nlen))
			continue;

		if(bits & TAG_DIR)
			level++;

		return nd;
	}

	return NULL;
}

/* Locate specified, node, make sure it's a directory, and return
   its first child. Note directories may be empty, in which case
   we'd return the next siblilng or even something more remote.
   That's fine, we'll detect it later by looking at its level. */

static struct node* locate_dir(CCT, struct node* nd, char* p, char* q)
{
	if(!(nd = locate_node(cct, nd, p, q)))
		return nd;
	if(!(nd->bits & TAG_DIR))
		nd = NULL;

	if(nd + 1 >= cct->ne) /* make sure (nd+1) is still valid */
		return NULL;

	int depth = cct->depth;

	if(depth >= MAXDEPTH)
		return NULL;

	cct->stack[depth] = nd;
	cct->depth = depth + 1;

	return nd + 1;
}

/* Given a path from the config,

      /lib/musl-x86
       ^           ^
       ls          le

   place marks to ensure this path, if present in the package,
   gets installed.

   To do that, we locate the terminal node (musl-x86 in this case),
   mark it, mark everything under it if it's a directory, and mark
   the path components leading to the node (lib in this case). */

static void mark_path(CCT, char* ls, char* le)
{
	struct node* nd = cct->ns;

	if(cct->depth)
		fail("non-zero initial depth", NULL, 0);

	cct->nopref = 1;

	char* p = ls;
	char* e = le;

	while(p < e) {
		char* q = strecbrk(p, e, '/');

		if(q >= e) /* last path component */
			break;

		if(!(nd = locate_dir(cct, nd, p, q)))
			return;

		p = q + 1;
	}

	if(p >= e) /* "a/b/c/", with trailing slash */
		mark_glob(cct, nd);
	else if(!(nd = locate_node(cct, nd, p, e)))
		goto out;
	else
		mark_leaf(cct, nd);

	if(cct->depth)
		mark_back(cct);
out:
	cct->depth = 0;
}

/* Go through the rules (from the config file) and make sure the tree
   of the .pac being installed is compliant. This is done by placing
   marks on each node in matching subtrees. */

static void apply_rules(CTX)
{
	struct configctx context, *cct = &context;

	memzero(cct, sizeof(*cct));

	cct->ctx = ctx;

	cct->ns = ctx->index;
	cct->ne = ctx->index + ctx->nodes;

	void* p = ctx->paths;
	void* e = ctx->paend;

	while(p < e) {
		struct path* pp = p;

		uint hlen = pp->hlen;
		uint flag = hlen & HLEN_MASK;

		p += (hlen & ~flag);

		uint mark = BIT_MARK;

		if(flag == HLEN_PASS)
			mark |= BIT_NEED;
		else if(flag == HLEN_DENY)
			mark |= BIT_DENY;
		else
			fail("invalid rule value", NULL, flag);

		cct->mark = mark;

		char* path = pp->str;
		char* pend = strpend(path);

		mark_path(cct, path, pend);
	}
}

/* Make sure we aren't unpacking ".." and other weird things. */

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

static void warn_node(CTX, struct node* nd, int err)
{
	warn("node", nd->name, err);
	ctx->fail = 1;
}

static void check_nodes(CTX)
{
	struct node* idx = ctx->index;
	uint i, n = ctx->nodes;

	for(i = 0; i < n; i++) {
		struct node* nd = &idx[i];
		int bits = nd->bits;

		if(bits & BIT_MARK) {
			if(bits & BIT_DENY)
				warn_node(ctx, nd, -EPERM);
		} else {
			int pol = ctx->policy;

			if(pol == POL_DENY_REST)
				warn_node(ctx, nd, -EPERM);
			else
				nd->bits |= BIT_MARK | BIT_NEED;
		}
	}

	if(ctx->fail) fail("unable to continue", NULL, 0);
}

static void check_names(CTX)
{
	struct node* idx = ctx->index;
	uint i, n = ctx->nodes;

	for(i = 0; i < n; i++) {
		struct node* nd = &idx[i];
		char* name = nd->name;

		if(!name[0])
			fail("empty node name", NULL, 0);
		if(name[0] == '.')
			fail("invalid node name:", name, 0);

		if(check_chars(name))
			fail("invalid node name:", name, 0);
	}
}

void check_index(CTX)
{
	check_names(ctx);

	apply_rules(ctx);

	check_nodes(ctx);
}
