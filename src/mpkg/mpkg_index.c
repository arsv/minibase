#include <sys/mman.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/prctl.h>
#include <sys/fprop.h>

#include <config.h>
#include <string.h>
#include <format.h>
#include <util.h>

#include "mpkg.h"

/* The point of the code here is to prepare the .pac file data for further
   processing. We read the whole header into memory, and index it, forming
   ctx->index[]. Variable-length sized are parsed at this stage into uints,
   to avoid re-doing it later. */

struct treectx {
	struct top* ctx;

	void* ptr; /* saved heap pointer */

	byte* p;
	byte* e;

	char* last;
	char* prev[MAXDEPTH]; /* saved last values */

	int indirs; /* we're past the leaf nodes within a directory */
};

#define TCX struct treectx* tcx

static void parse_size(TCX, struct node* nd)
{
	byte* p = tcx->p;
	byte* e = tcx->e;
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
	tcx->p = p;
}

static void parse_name(TCX, struct node* nd)
{
	byte* p = tcx->p;
	byte* e = tcx->e;
	char* name = (char*)p;
	byte* z = p + PAGE;

	if(e > z) e = z; /* no names over 4096 chars long */

	while(p < e && *p)
		p++;
	if(p >= e)
		fail("non-terminated name in index", NULL, 0);

	nd->name = name;
	tcx->p = p + 1;
}

/* We store path for error messages and such, we also store the last
   name seen at this depth for the entry order check above. */

static void enter_dir(TCX, struct node* nd)
{
	struct top* ctx = tcx->ctx;
	int depth = ctx->depth;

	if(depth >= MAXDEPTH)
		fail("tree depth exceeded", NULL, 0);

	char* name = nd->name;

	ctx->depth = depth + 1;
	ctx->path[depth] = name;
	tcx->prev[depth] = name;
	tcx->last = NULL;
	tcx->indirs = 0;
}

static void rewind_path(TCX, int todepth)
{
	struct top* ctx = tcx->ctx;
	int depth = ctx->depth;

	if(!tcx->indirs) {
		tcx->last = NULL;
		tcx->indirs = 1;
	}

	if(todepth > depth)
		fail("invalid index entry", NULL, 0);
	if(todepth == depth)
		return;

	ctx->depth = todepth;
	tcx->last = tcx->prev[todepth];
}

static struct node* new_node(TCX, int lead)
{
	struct node* nd = alloc_exact(tcx->ctx, sizeof(*nd));

	memzero(nd, sizeof(*nd));

	nd->bits = lead;

	return nd;
}

static int take_node(TCX)
{
	byte* p = tcx->p;
	byte* e = tcx->e;

	if(p >= e)
		return 0;

	int lead = *p++;

	tcx->p = p;

	if(lead & TAG_DIR)
		rewind_path(tcx, lead & TAG_DEPTH);

	struct node* nd = new_node(tcx, lead);

	if(!(lead & TAG_DIR))
		parse_size(tcx, nd);

	parse_name(tcx, nd);

	if(lead & TAG_DIR)
		enter_dir(tcx, nd);

	return 1;
}

static void init_treeidx(CTX, TCX)
{
	memzero(tcx, sizeof(*tcx));

	void* p = ctx->head + ctx->hoff;
	void* e = p + ctx->hlen;

	ctx->depth = 0;

	tcx->p = p;
	tcx->e = e;

	tcx->ptr = ctx->ptr;

	tcx->ctx = ctx;
}

static void fini_treeidx(CTX, TCX)
{
	struct node* brk = tcx->ptr;
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
	struct treectx context, *tcx = &context;

	init_treeidx(ctx, tcx);

	while(take_node(tcx))
		;

	fini_treeidx(ctx, tcx);
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

static void load_index(CTX)
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
}

static void alloc_transfer_buf(CTX)
{
	uint size = 1<<20;
	uint prot = PROT_READ | PROT_WRITE;
	uint flags = MAP_PRIVATE | MAP_ANONYMOUS;

	void* buf = sys_mmap(NULL, size, prot, flags, -1, 0);
	int ret;

	if(ctx->databuf && ctx->datasize != size)
		fail(NULL, NULL, -EFAULT);

	if((ret = mmap_error(buf)))
		fail("mmap", NULL, ret);

	ctx->databuf = buf;
	ctx->datasize = size;
}

static void spawn_pipe(CTX, char* dec, char* path)
{
	int ret, pid, fds[2];

	alloc_transfer_buf(ctx);

	if((ret = sys_pipe(fds)) < 0)
		fail("pipe", NULL, ret);

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, 0);

	if(pid == 0) {
		char* args[] = { dec, path, NULL };

		if((ret = sys_prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0)) < 0)
			fail("prctl", NULL, ret);
		if((ret = sys_dup2(fds[1], 1)) < 0)
			fail("dup2", NULL, ret);
		if((ret = sys_close(fds[0])) < 0)
			fail("close", NULL, ret);
		if((ret = sys_close(fds[1])) < 0)
			fail("close", NULL, ret);

		ret = sys_execve(*args, args, ctx->envp);

		fail("execve", *args, ret);
	}

	if((ret = sys_close(fds[1])) < 0)
		fail("close", NULL, ret);

	ctx->pacfd = fds[0];
}

static void open_compressed(CTX)
{
	int ret;

	char* pacpath = ctx->pacname;
	char* suffix = ctx->suffix;

	char* dir = BASE_ETC "/pac/";
	int len = strlen(dir) + strlen(suffix) + 2;
	char* decpath = alloca(len);

	char* p = decpath;
	char* e = decpath + len - 1;

	p = fmtstr(p, e, dir);
	p = fmtstr(p, e, suffix);

	*p++ = '\0';

	if((ret = sys_access(decpath, X_OK)) < 0)
		fail(NULL, decpath, ret);

	spawn_pipe(ctx, decpath, pacpath);
}

static void open_uncompressed(CTX)
{
	int fd;
	char* name = ctx->pacname;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		fail(NULL, name, fd);

	ctx->pacfd = fd;
}

void load_pacfile(CTX)
{
	if(ctx->suffix)
		open_compressed(ctx);
	else
		open_uncompressed(ctx);

	load_index(ctx);

	index_nodes(ctx);
}
