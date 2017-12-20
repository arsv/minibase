#include <sys/file.h>
#include <sys/dents.h>
#include <sys/mman.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <output.h>
#include <util.h>

ERRTAG("ps");

#define OPTS "k"
#define OPT_k (1<<0)

struct proc {
	ushort len; /* of the whole structure, incl. name[] */

	int pid;
	int ppid;
	int uid;

	/* both *idx nodes are indexes into top.procs[] */
	int ridx; /* right, first child */
	int didx; /* down, next sibling */

	char name[];
};

struct top {
	int opts;

	void* brk;
	void* ptr;
	void* end;

	int nprocs;
	struct proc** procs;

	struct bufout bo;
};

#define CTX struct top* ctx

static void init_heap(CTX)
{
	void* brk = sys_brk(0);

	ctx->brk = brk;
	ctx->ptr = brk;
	ctx->end = brk;
}

static void* alloc(CTX, int len)
{
	void* ret = ctx->ptr;
	void* end = ctx->end;
	void* ptr = ret + len;

	if(ptr > end) {
		void* new = sys_brk(end + pagealign(ptr - end));

		if(new <= end)
			fail("cannot allocate memory", NULL, 0);

		ctx->end = new;
	};

	ctx->ptr = ptr;

	return ret;
}

/* Proc entries are read into a growing heap (as structs proc),
   then the heap is indexed.

   The status files, /proc/$$/status, contain lots of stuff we
   don't need, and all we need is always near the top of the file,
   so only a 512-byte head is read. */

static void index_entries(CTX, void* ptr, void* end)
{
	int nprocs = ctx->nprocs;
	struct proc** idx = alloc(ctx, nprocs*sizeof(void*));
	int i = 0;

	while(ptr < end) {
		struct proc* ps = ptr;
		ptr += ps->len;

		if(!ps->len || i >= nprocs)
			fail("corrupt proc index", NULL, 0);

		idx[i++] = ps;
	}

	ctx->procs = idx;
}

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static char* skipspace(char* p, char* e)
{
	for(; p < e; p++)
		if(!isspace(*p))
			break;

	return p;
}

static void set_int(char* p, int* dst)
{
	(void)parseint(p, dst);
}

static void parse_status(CTX, char* buf, int len)
{
	char* end = buf + len;
	char* ls;
	char* le;

	char* name = NULL;
	int pid = 0, ppid = 0, uid = 0;

	for(ls = buf; ls < end; ls = le + 1) {
		if((le = strecbrk(ls, end, '\n')) >= end)
			break;

		char* key = ls;
		char* val;

		if((val = strecbrk(ls, le, ':')) >= le)
			break;

		*le = '\0'; *val++ = '\0';
		val = skipspace(val, le);

		if(!strcmp(key, "Name"))
			name = val;
		else if(!strcmp(key, "Pid"))
			set_int(val, &pid);
		else if(!strcmp(key, "PPid"))
			set_int(val, &ppid);
		else if(!strcmp(key, "Uid"))
			set_int(val, &uid);
	}

	if(!name) return;

	int nlen = strlen(name);
	int plen = sizeof(struct proc) + nlen + 1;
	struct proc* ps = alloc(ctx, plen);

	ps->len = plen;
	ps->pid = pid;
	ps->ppid = ppid;
	ps->uid = uid;

	ps->ridx = -1;
	ps->didx = -1;

	memcpy(ps->name, name, nlen + 1);

	ctx->nprocs++;
}

static void read_proc_status(CTX, int at, char* pstr)
{
	char buf[512];
	int fd, rd;

	FMTBUF(p, e, path, 40);
	p = fmtstr(p, e, pstr);
	p = fmtstr(p, e, "/status");
	FMTEND(p, e);

	if((fd = sys_openat(at, path, O_RDONLY)) < 0)
		return;
	if((rd = sys_read(fd, buf, sizeof(buf))) < 0)
		return;

	sys_close(fd);

	return parse_status(ctx, buf, rd);
}

static int isdigit(int c)
{
	return (c >= '0' && c <= '9');
}

static void read_proc_list(CTX)
{
	int fd, rd;
	char* dir = "/proc";
	char buf[2048];

	if((fd = sys_open(dir, O_DIRECTORY)) < 0)
		fail(NULL, dir, fd);

	void* ptr = ctx->ptr;

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;
			ptr += de->reclen;

			if(de->reclen <= 0)
				break;
			if(de->type != DT_DIR)
				continue;
			if(!isdigit(de->name[0]))
				continue;

			read_proc_status(ctx, fd, de->name);
		}
	}

	void* end = ctx->ptr;

	sys_close(fd);

	index_entries(ctx, ptr, end);
}

/* The proc data comes with (pid, ppid) pairs from which a full tree
   must be built. The following is an awful awful O(n^2) algorithm.

   The entries are already sorted by pid, no need to sort them again
   or disturb their order. */

static void build_ps_tree(CTX)
{
	int nprocs = ctx->nprocs;
	struct proc** idx = ctx->procs;

	for(int i = 0; i < nprocs; i++) {
		struct proc* psi = idx[i];
		struct proc* chi = NULL;
		int pid = psi->pid;
		int ridx = -1;

		for(int j = 0; j < nprocs; j++) {
			struct proc* psj = idx[j];

			if(psj->ppid != pid)
				continue;

			if(ridx < 0)
				ridx = j;
			if(chi)
				chi->didx = j;

			chi = psj;
		}

		psi->ridx = ridx;
	}
}

/* Output, depth-first tree traversal. Top-level entries are treated
   differently from the reset because they don't get any prefix. */

static void dump_proc(struct bufout* bo, struct proc* ps, char* pref, int plen)
{
	FMTBUF(p, e, buf, strlen(ps->name) + 20);
	p = fmtint(p, e, ps->pid);
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, ps->name);
	FMTENL(p, e);

	bufout(bo, pref, plen);
	bufout(bo, buf, p - buf);
}

static char* prep_curr_pref(char* p, char* e, int didx)
{
	if(didx > 0)
		p = fmtstr(p, e, "├ ");
	else
		p = fmtstr(p, e, "└ ");

	return p;
}

static char* prep_next_pref(char* p, char* e, int didx)
{
	if(didx > 0)
		p = fmtstr(p, e, "│ ");
	else
		p = fmtstr(p, e, "  ");

	return p;
}

static void dump_rec(CTX, int pi, char* prev, int plen)
{
	struct bufout* bo = &ctx->bo;
	char pref[plen+5];
	char* p = pref;
	char* e = pref + sizeof(pref);
	char* q = fmtraw(p, e, prev, plen);

	while(pi >= 0) {
		struct proc* ps = ctx->procs[pi];
		int didx = ps->didx;
		int ridx = ps->ridx;

		p = prep_curr_pref(q, e, didx);
		dump_proc(bo, ps, pref, p - pref);

		p = prep_next_pref(q, e, didx);
		dump_rec(ctx, ridx, pref, p - pref);

		pi = didx;
	}
}

static void dump_top(CTX, int pi)
{
	struct bufout* bo = &ctx->bo;
	struct proc* ps = ctx->procs[pi];

	char* pref = "";
	int plen = 0;

	dump_proc(bo, ps, pref, plen);

	dump_rec(ctx, ps->ridx, pref, plen);
}

static void dump_kernel(CTX)
{
	int i, nprocs = ctx->nprocs;
	struct proc** idx = ctx->procs;

	for(i = 1; i < nprocs; i++)
		if(!idx[i]->ppid)
			dump_top(ctx, i);
}

static void dump_proc_list(CTX)
{
	int len = PAGE;
	struct bufout* bo = &ctx->bo;

	bo->fd = STDOUT;
	bo->ptr = 0;
	bo->len = len;
	bo->buf = alloc(ctx, len);

	if(ctx->opts & OPT_k)
		dump_kernel(ctx);
	else
		dump_top(ctx, 0);

	bufoutflush(bo);
}

/* Entry point */

int main(int argc, char** argv)
{
	int i = 1;
	struct top context, *ctx = &context;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		ctx->opts = argbits(OPTS, argv[i++] + 1);

	init_heap(ctx);

	read_proc_list(ctx);

	build_ps_tree(ctx);

	dump_proc_list(ctx);

	return 0;
}
