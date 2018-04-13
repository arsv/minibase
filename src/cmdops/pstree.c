#include <sys/file.h>
#include <sys/dents.h>
#include <sys/mman.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <output.h>
#include <util.h>

ERRTAG("pstree");

#define OPTS "k"
#define OPT_k (1<<0)

struct proc {
	ushort len; /* of the whole structure, incl. name[] */

	int pid;
	int ppid;
	int uid;
	int euid;

	/* idx nodes are indexes into top.procs[] */
	int pidx; /* parent node */
	int ridx; /* right, first child */
	int didx; /* down, next sibling */

	int mark;

	char name[];
};

struct trec {
	char* buf;
	int sep;
	int len;
	int end;

	int uid;
	int euid;
};

struct top {
	int opts;

	void* brk;
	void* ptr;
	void* end;

	int nprocs;
	struct proc** procs;

	char* passwd;
	int pwlen;
	int pwerr;

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

static void set_uids(char* p, int* uid, int* euid)
{
	if(!(p = parseint(p, uid)))
		return;
	while(*p && isspace(*p))
		p++;

	(void)parseint(p, euid);
}

static void parse_status(CTX, char* buf, int len)
{
	char* end = buf + len;
	char* ls;
	char* le;

	char* name = NULL;
	int pid = 0, ppid = 0, euid = 0, uid = 0;

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
			set_uids(val, &uid, &euid);
	}

	if(!name) return;

	int nlen = strlen(name);
	int plen = sizeof(struct proc) + nlen + 1;
	struct proc* ps = alloc(ctx, plen);

	ps->len = plen;
	ps->pid = pid;
	ps->ppid = ppid;
	ps->uid = uid;
	ps->euid = euid;

	ps->pidx = -1;
	ps->ridx = -1;
	ps->didx = -1;
	ps->mark = 0;

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

			psj->pidx = i;

			if(ridx < 0)
				ridx = j;
			if(chi)
				chi->didx = j;

			chi = psj;
		}

		psi->ridx = ridx;
	}
}

/* Selective output, only show nodes with given names and
   the branches leading to them. This is done by marking
   the nodes we want to see and re-linking the tree to remove
   everything else. */

static void mark_upwards(CTX, int i)
{
	struct proc** idx = ctx->procs;

	while(i >= 0) {
		struct proc* ps = idx[i];

		if(ps->mark) break;

		ps->mark = 1;

		i = ps->pidx;
	}
}

static void find_mark_upwards(CTX, char* name)
{
	int i, nprocs = ctx->nprocs;
	struct proc** idx = ctx->procs;

	int pid;
	char* p;

	if((p = parseint(name, &pid)) && !*p) {
		for(i = 0; i < nprocs; i++) {
			if(idx[i]->pid == pid) {
				mark_upwards(ctx, i);
				break;
			}
		}
	} else {
		int nlen = strlen(name);

		for(i = 0; i < nprocs; i++) {
			if(!strncmp(name, idx[i]->name, nlen))
				mark_upwards(ctx, i);
		}
	}
}

static void trim_subtree(CTX, int pi)
{
	struct proc** idx = ctx->procs;
	struct proc* pp = idx[pi];

	int i = pp->ridx;
	int last = -1;

	while(i >= 0) {
		struct proc* ps = idx[i];

		if(!ps->mark)
			goto next;

		if(last < 0)
			pp->ridx = i;
		else
			idx[last]->didx = i;

		last = i;

		if(ps->ridx >= 0)
			trim_subtree(ctx, i);

		next: i = ps->didx;
	}

	if(last >= 0)
		idx[last]->didx = -1;
	else
		pp->ridx = -1;
}

static void trim_unmarked_branches(CTX)
{
	int i, nprocs = ctx->nprocs;
	struct proc** idx = ctx->procs;

	for(i = 0; i < nprocs; i++) {
		struct proc* ps = idx[i];

		if(ps->ppid)
			continue;
		if(!ps->mark)
			continue;
		if(ps->ridx < 0)
			continue;

		trim_subtree(ctx, i);
	}
}

static void only_leave_named(CTX, int nargs, char** args)
{
	if(nargs <= 0)
		return;

	for(int i = 0; i < nargs; i++)
		find_mark_upwards(ctx, args[i]);

	trim_unmarked_branches(ctx);
}

/* User name resolution */

static int read_passwd(CTX)
{
	int fd, ret;
	struct stat st;
	char* name = "/etc/passwd";

	ctx->pwerr = -1;

	if((fd = sys_open(name, O_RDONLY)) < 0)
		return fd;
	if((ret = sys_fstat(fd, &st)) < 0)
		goto out;

	const int prot = PROT_READ;
	const int flags = MAP_PRIVATE;

	void* buf = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if((ret = mmap_error(buf)))
		goto out;

	ctx->passwd = buf;
	ctx->pwlen = st.size;
	ctx->pwerr = ret = 0;
out:
	sys_close(fd);

	return ret;
}

static int make_uid_str(char* buf, int len, int uid)
{
	char* p = buf;
	char* e = buf + len;

	p = fmtint(p, e, uid);

	return p - buf;
}

char* fmt_user(char* p, char* e, CTX, int uid)
{
	char uidstr[20];
	int uidlen = make_uid_str(uidstr, sizeof(uidstr), uid);

	if(ctx->passwd)
		;
	else if(ctx->pwerr)
		goto asint;
	else if(read_passwd(ctx) < 0)
		goto asint;

	char* buf = ctx->passwd;
	char* end = buf + ctx->pwlen;

	char* ls;
	char* le;

	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');

		char* ns = ls;                     /* 1st field, name */
		char* ne = strecbrk(ls, le, ':');
		char* ps = ne + 1;                 /* 2nd field, password */
		char* pe = strecbrk(ps, le, ':');
		char* is = pe + 1;                 /* 3rd field, id */
		char* ie = strecbrk(is, le, ':');

		int ilen = ie - is;

		if(ilen != uidlen)
			continue;
		if(strncmp(uidstr, is, ilen))
			continue;

		return fmtraw(p, e, ns, ne - ns);
	}
asint:
	return fmtint(p, e, uid);
}

/* Output, depth-first tree traversal. Top-level entries are treated
   differently from the reset because they don't get any prefix. */

static void dump_proc(CTX, struct bufout* bo, struct trec* tr, struct proc* ps)
{
	bufout(bo, tr->buf, tr->len);

	char* name = ps->name;
	int len = strlen(name);

	FMTBUF(p, e, buf, len + 100);

	p = fmtint(p, e, ps->pid);
	p = fmtstr(p, e, " ");
	p = fmtstr(p, e, ps->name);

	if(tr->uid != ps->uid || tr->euid != ps->euid) {
		p = fmtstr(p, e, " [");
		p = fmt_user(p, e, ctx, ps->uid);
		if(ps->euid != ps->uid) {
			p = fmtstr(p, e, "/");
			p = fmt_user(p, e, ctx, ps->euid);
		}
		p = fmtstr(p, e, "]");
	}

	*p++ = '\n';

	bufout(bo, buf, p - buf);
}

static void prep_curr_pref(struct trec* tr, int didx, struct trec* tp)
{
	char* p = tr->buf + tr->sep;
	char* e = tr->buf + tr->end;

	if(didx > 0)
		p = fmtstr(p, e, "├ ");
	else
		p = fmtstr(p, e, "└ ");

	tr->len = p - tr->buf;

	tr->uid = tp->uid;
	tr->euid = tp->euid;
}

static void prep_next_pref(struct trec* tr, int didx, struct proc* ps)
{
	char* p = tr->buf + tr->sep;
	char* e = tr->buf + tr->end;

	if(didx > 0)
		p = fmtstr(p, e, "│ ");
	else
		p = fmtstr(p, e, "  ");

	tr->len = p - tr->buf;

	tr->uid = ps->uid;
	tr->euid = ps->euid;
}

static void prep_tree_rec(struct trec* tr, struct trec* tp, char* buf, int size)
{
	tr->buf = buf;
	tr->end = size;

	char* p = buf;
	char* e = buf + size;

	p = fmtraw(p, e, tp->buf, tp->len);

	tr->sep = p - buf;
	tr->len = p - buf;
}

static void dump_rec(CTX, struct trec* tp, int pi)
{
	struct bufout* bo = &ctx->bo;
	struct trec trec, *tr = &trec;
	char pref[tp->len+5];

	prep_tree_rec(tr, tp, pref, sizeof(pref));

	while(pi >= 0) {
		struct proc* ps = ctx->procs[pi];
		int didx = ps->didx;
		int ridx = ps->ridx;

		prep_curr_pref(tr, didx, tp);
		dump_proc(ctx, bo, tr, ps);

		prep_next_pref(tr, didx, ps);
		dump_rec(ctx, tr, ridx);

		pi = didx;
	}
}

static void dump_top(CTX, int pi)
{
	struct bufout* bo = &ctx->bo;
	struct proc* ps = ctx->procs[pi];

	struct trec tr = {
		.buf = "",
		.sep = 0,
		.len = 0,
		.uid = 0,
		.euid = 0
	};

	dump_proc(ctx, bo, &tr, ps);
	dump_rec(ctx, &tr, ps->ridx);
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

	only_leave_named(ctx, argc - i, argv + i);

	dump_proc_list(ctx);

	return 0;
}
