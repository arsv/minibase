#include <sys/file.h>
#include <sys/mman.h>

#include <config.h>
#include <format.h>
#include <string.h>
#include <util.h>
#include <main.h>

#include "mpkg.h"

#define CONFIG BASE_ETC "/mpkg.conf"

/* With the index loaded and the tree re-built, we need to verify that
   the package is compliant with the local policy, that is, it does not
   install stuff outside of allowed directories. If that's the case, we
   report offending paths and abort.

   While doing that, we also put marks on index nodes to indicate which
   files we want to install (BIT_NEED) and which we want to skip (no
   BIT_NEED). */

#define MAXCONF (1<<20)

struct configctx {
	struct top* ctx;
	char* repo;
	char* prefix;
	int line;
	int active;
	int gotact;
	int marked;
	int nopref;

	struct node* ns;
	struct node* ne;

	int mark;
	uint depth;
	struct node* stack[MAXDEPTH];
};

#define CCT struct configctx* cct

static void report_syntax(CCT, char* msg)
{
	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, CONFIG);
	p = fmtstr(p, e, ":");
	p = fmtint(p, e, cct->line);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, msg);
	FMTENL(p, e);

	(void)sys_write(STDERR, buf, p - buf);

	_exit(0xFF);
}

static void check_marks(CCT)
{
	struct top* ctx = cct->ctx;
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	for(; nd < ne; nd++) {
		int bits = nd->bits;
		char* name = nd->name;

		if(!(bits & BIT_MARK))
			warnx(ctx, NULL, name, -EPERM);

		if(!(bits & TAG_DIR))
			continue;

		int depth = ctx->depth;
		int lvl = bits & TAG_DEPTH;

		if(depth < lvl)
			continue;
		if(depth > lvl)
			depth = lvl;
		if(depth >= MAXDEPTH)
			warnx(ctx, NULL, name, -ELOOP);

		ctx->path[depth] = name;
		ctx->depth = depth + 1;
	}

	ctx->depth = 0;

	if(ctx->fail) fail("unable to continue", NULL, 0);
}

static void mark_all_nodes(CCT)
{
	struct top* ctx = cct->ctx;
	struct node* nd = ctx->index;
	struct node* ne = nd + ctx->nodes;

	for(; nd < ne; nd++) {
		int bits = nd->bits;

		if(bits & BIT_MARK) continue;

		nd->bits = bits | BIT_MARK | BIT_NEED;
	}
}

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

static void tree_mark(CCT, char* ls, char* le)
{
	cct->marked = 1;
	cct->mark = BIT_MARK | BIT_NEED;

	mark_path(cct, ls, le);
}

static void tree_skip(CCT, char* ls, char* le)
{
	cct->mark = BIT_MARK;

	mark_path(cct, ls, le);
}

static void group_prefix(CCT, char* ls, char* le)
{
	struct top* ctx = cct->ctx;

	if(cct->prefix)
		report_syntax(cct, "duplicate prefix directive");
	if(cct->nopref)
		report_syntax(cct, "misplaced prefix directive");

	int len = le - ls;
	char* buf = heap_alloc(ctx, len + 1);

	memcpy(buf, ls, len);
	buf[len] = '\0';

	cct->prefix = buf;
}

static void enter_section(CCT, char* ls, char* le)
{
	char* repo = cct->repo;
	int active = 0;

	if(!repo)
		goto out;

	int rlen = strlen(repo);
	int slen = le - ls;

	if(rlen != slen)
		goto out;
	if(memcmp(repo, ls, slen))
		goto out;

	active = 1;
	cct->gotact = active;
	cct->nopref = 0;
out:
	cct->active = active;
}

static void parse_line(CCT, char* ls, char* le)
{
	if(ls >= le) return; /* empty line */

	char lead = *ls++;

	if(lead == '#')
		return;
	if(lead == '@')
		return enter_section(cct, ls, le);
	if(!cct->active)
		return;
	if(lead == '=')
		return group_prefix(cct, ls, le);
	if(lead == '/')
		return tree_mark(cct, ls, le);
	if(lead == '-')
		return tree_skip(cct, ls, le);

	report_syntax(cct, "invalid syntax");
}

static void report_repo(CCT)
{
	struct top* ctx = cct->ctx;

	if(ctx->repo)
		fail("no rules defined for this repo", NULL, 0);
	else
		fail("no default deploy rules defined", NULL, 0);
}

static int grpdone(CCT)
{
	return (cct->gotact && !cct->active);
}

static void mark_valid(CCT)
{
	struct top* ctx = cct->ctx;
	char* ptr = ctx->cbrk;
	char* end = ptr + ctx->clen;

	while(ptr < end) {
		char* ls = ptr;
		char* le = strecbrk(ptr, end, '\n');

		if(le >= end) break;

		cct->line++;

		parse_line(cct, ls, le);

		if(grpdone(cct)) break;

		ptr = le + 1;
	}
}

void load_config(CTX)
{
	int fd, ret;
	int at = ctx->rootfd;
	struct stat st;
	char* name = CONFIG;

	name = root_adjust(name);

	if((fd = sys_openat(at, name, O_RDONLY)) < 0)
		fail(NULL, name, fd);
	if((ret = sys_fstat(fd, &st)) < 0)
		fail("stat", name, ret);

	if(st.size >= MAXCONF)
		fail(NULL, name, -E2BIG);

	uint size = st.size;
	int proto = PROT_READ;
	int flags = MAP_PRIVATE;
	void* buf = sys_mmap(NULL, size, proto, flags, fd, 0);

	if((ret = mmap_error(buf)))
		fail("mmap", name, ret);

	ctx->cbrk = buf;
	ctx->clen = size;
}

static void check_group(CCT)
{
	if(cct->marked)
		;
	else if(cct->prefix)
		mark_all_nodes(cct);
	else
		report_repo(cct);

	char* prefix = cct->prefix;

	if(!prefix) return;

	setup_prefix(cct->ctx, prefix);
}

static void init_context(CTX, CCT)
{
	char* repo = ctx->repo;

	memzero(cct, sizeof(*cct));

	cct->ctx = ctx;
	cct->repo = repo;
	cct->active = repo ? 0 : 1;
	cct->gotact = cct->active;

	cct->ns = ctx->index;
	cct->ne = ctx->index + ctx->nodes;
}

void check_config(CTX)
{
	struct configctx context, *cct = &context;

	init_context(ctx, cct);

	mark_valid(cct);

	check_group(cct);

	check_marks(cct);
}
