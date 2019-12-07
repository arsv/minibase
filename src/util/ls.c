#include <bits/ioctl/tty.h>
#include <sys/file.h>
#include <sys/dents.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <format.h>
#include <string.h>
#include <memoff.h>
#include <output.h>
#include <util.h>
#include <main.h>

ERRTAG("ls");

#define MAYBEDIR  0
#define MUSTBEDIR 1

#define DT_LNK_DIR 71	/* symlink pointing to a dir, custom value */

#define OPTS "acdnluy"
#define OPT_a (1<<0)
#define OPT_c (1<<1)
#define OPT_d (1<<2)
#define OPT_n (1<<3)
#define OPT_l (1<<4)
#define OPT_u (1<<5)
#define OPT_y (1<<6)

struct ent {
	int type;
	int mode;
	int uid;
	int gid;
	uint64_t size;
	unsigned namelen;
	char name[];
};

struct mmaped {
	char* buf;
	size_t len;
};

struct top {
	int opts;
	struct bufout bo;
	int fd;

	int npatt;
	char** patt;

	void* base;
	void* ptr;
	void* end;

	struct ent** idx;

	int sizelen;
	int uidlen;
	int gidlen;

	struct mmaped passwd;
	struct mmaped group;
};

#define CTX struct top* ctx __unused

char outbuf[PAGE];

static void mmap_file(struct mmaped* mp, const char* fname)
{
	int fd, ret;
	struct stat st;

	memzero(mp, sizeof(*mp));

	if((fd = sys_open(fname, O_RDONLY)) < 0)
		return;
	if((ret = sys_fstat(fd, &st)) < 0)
		return;
	if(mem_off_cmp(0x7FFFFFFF, st.size) < 0)
		return; /* file too large */

	const int prot = PROT_READ;
	const int flags = MAP_SHARED;
	void* buf = sys_mmap(NULL, st.size, prot, flags, fd, 0);

	if(mmap_error(buf))
		return;

	mp->len = st.size;
	mp->buf = buf;
}

static void init_context(CTX, int opts)
{
	void* brk = sys_brk(0);
	void* end = sys_brk(brk + PAGE);
	int fd;

	if(brk_error(brk, end))
		fail("cannot initialize heap", NULL, 0);

	ctx->opts = opts;

	ctx->base = brk;
	ctx->end = end;
	ctx->ptr = brk;

	ctx->bo.fd = 1;
	ctx->bo.buf = outbuf;
	ctx->bo.len = sizeof(outbuf);
	ctx->bo.ptr = 0;

	if((fd = sys_open(".", O_DIRECTORY)) < 0)
		fail("open", ".", fd);

	ctx->fd = fd;

	if(opts & OPT_l) {
		mmap_file(&ctx->passwd, "/etc/passwd");
		mmap_file(&ctx->group,  "/etc/group");
	}
}

static void fini_context(CTX)
{
	bufoutflush(&ctx->bo);
}

static void* alloc(CTX, int len)
{
	char* ret;

	if(ctx->ptr + len < ctx->end)
		goto got;

	int need = len + PAGE - (len % PAGE);

	void* old = ctx->end;
	void* brk = sys_brk(old + need);

	if(brk_error(old, brk))
		fail("brk", NULL, 0);

	ctx->end = brk;
got:
	ret = ctx->ptr;
	ctx->ptr += len;

	return ret;
}

static void stat_entry(CTX, int at, struct ent* en)
{
	char* name = en->name;
	int flags = AT_NO_AUTOMOUNT | AT_SYMLINK_NOFOLLOW;
	struct stat st;

	if((sys_fstatat(at, name, &st, flags)) < 0)
		return;

	en->mode = st.mode;
	en->size = st.size;
	en->uid = st.uid;
	en->gid = st.gid;
}

static void stat_target(CTX, int at, struct ent* en)
{
	char* name = en->name;
	int flags = AT_NO_AUTOMOUNT;
	struct stat st;

	if(en->type != DT_LNK)
		return;
	if(sys_fstatat(at, name, &st, flags) < 0)
		return;
	if(S_ISDIR(st.mode))
		en->type = DT_LNK_DIR;
}

static void add_dirent(CTX, int at, struct dirent* de)
{
	int opts = ctx->opts;
	void* ptr = ctx->ptr;

	char* name = de->name;
	int len = strlen(name);

	struct ent* en = alloc(ctx, sizeof(struct ent) + len + 1);

	memzero(en, sizeof(*en));

	en->namelen = len;
	en->type = de->type;
	memcpy(en->name, name, len + 1);

	stat_entry(ctx, at, en);
	stat_target(ctx, at, en);

	if((opts & OPT_d) && (en->type != DT_DIR && en->type != DT_LNK_DIR))
		ctx->ptr = ptr; /* undo entry */
}

static int match(CTX, char* name)
{
	int all = (ctx->opts & OPT_a);
	int npatt = ctx->npatt;
	char** patt = ctx->patt;

	if(name[0] == '.' && !all)
		return 0;
	if(npatt <= 0)
		return 1;

	for(int i = 0; i < npatt; i++)
		if(strstr(name, patt[i]))
			return 1;

	return 0;
}

static void read_whole(CTX)
{
	char buf[2048];
	int rd, fd = ctx->fd;

	while((rd = sys_getdents(fd, buf, sizeof(buf))) > 0) {
		void* ptr = buf;
		void* end = buf + rd;

		while(ptr < end) {
			struct dirent* de = ptr;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;
			if(!match(ctx, de->name))
				continue;

			add_dirent(ctx, fd, de);
		}
	} if(rd < 0) {
		fail("getdents", NULL, rd);
	}
}

static int entlen(void* p)
{
	struct ent* en = p;
	return sizeof(*en) + en->namelen + 1;
}

static void index_entries(CTX, void* ents, void* eend)
{
	void* p;
	int nument = 0;

	for(p = ents; p < eend; p += entlen(p))
		nument++;

	int len = (nument+1) * sizeof(void*);
	struct ent** idx = alloc(ctx, len);
	struct ent** end = idx + len;
	struct ent** ptr = idx;

	for(p = ents; p < eend; p += entlen(p)) {
		if(ptr >= end)
			break;
		*(ptr++) = p;
	}

	*ptr = NULL;

	ctx->idx = idx;
}

static int isdirtype(int t)
{
	return (t == DT_DIR || t == DT_LNK_DIR);
}

static int cmpidx(const void* a, const void* b, long opts)
{
	struct ent* pa = *((struct ent**)a);
	struct ent* pb = *((struct ent**)b);

	if(!(opts & OPT_u)) {
		int dira = isdirtype(pa->type);
		int dirb = isdirtype(pb->type);

		if(dira && !dirb)
			return -1;
		if(dirb && !dira)
			return  1;
	}

	return strcmp(pa->name, pb->name);
}

static void sort_indexed(CTX)
{
	int opts = ctx->opts;
	struct ent** idx = ctx->idx;
	struct ent** p;
	int count = 0;

	for(p = idx; *p; p++)
		count++;

	qsortx(idx, count, sizeof(void*), cmpidx, opts);
}

static char* fmt_name(char* p, char* e, int id, struct mmaped* mp)
{
	char idstr[20];
	char* idend = idstr + sizeof(idstr) - 1;
	char* idptr = fmti32(idstr, idend, id);
	*idptr++ = ':';
	int idlen = idptr - idstr;

	char* buf = mp->buf;
	char* end = buf + mp->len;

	if(!buf) goto asnum;

	char *ls, *le; /* line start/end */
	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');

		char* ns = ls;                     /* 1st field, name */
		char* ne = strecbrk(ls, le, ':');
		char* ps = ne + 1;                 /* 2nd field, password */
		char* pe = strecbrk(ps, le, ':');
		char* is = pe + 1;                 /* 3rd field, id */
		char* ie = strecbrk(is, le, ':');

		if(ie >= le)
			continue;
		if(strncmp(idstr, is, idlen))
			continue;

		return fmtstrn(p, e, ns, ne - ns);
	}
asnum:
	return fmtstrn(p, e, idstr, idlen - 1);
}

/* Very dumb and straightforward approach for now. Definitely needs
   some sort of caching here, in most cases all or almost all files
   in the directory are owned by just one or two ids. */

static char* fmt_uid(char* p, char* e, int id, CTX)
{
	return fmt_name(p, e, id, &ctx->passwd);
}

static char* fmt_gid(char* p, char* e, int id, CTX)
{
	return fmt_name(p, e, id, &ctx->group);
}

static int numlen(uint64_t n)
{
	int len = 1;

	while(n >= 10) {
		n /= 10;
		len++;
	}

	return len;
}

static int kmglen(uint64_t n)
{
	char buf[20];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	return fmtu64(p, e, n) - p;
}

static void set_max_size_len(CTX)
{
	FMTBUF(p, e, buf, 50);
	struct ent** ep = ctx->idx;
	int shortfmt = !(ctx->opts & OPT_l);

	int maxsizelen = 0;
	int maxuidlen = 0;
	int maxgidlen = 0;

	int numeric = (ctx->opts & OPT_n);

	for(; *ep; ep++) {
		struct ent* en = *ep;
		uint64_t size = en->size;

		int sizelen = numeric ? numlen(size) : kmglen(size);

		if(sizelen > maxsizelen)
			maxsizelen = sizelen;

		if(shortfmt)
			continue;

		int uidlen = fmt_uid(p, e, en->uid, ctx) - p;
		int gidlen = fmt_gid(p, e, en->gid, ctx) - p;

		if(uidlen > maxuidlen)
			maxuidlen = uidlen;
		if(gidlen > maxgidlen)
			maxgidlen = gidlen;
	}

	ctx->sizelen = maxsizelen;
	ctx->uidlen = maxuidlen;
	ctx->gidlen = maxgidlen;
}

static char* fmt_mode(char* p, char* e, struct ent* en)
{
	int mode = en->mode;

	p = fmtchar(p, e, '-');

	p = fmtchar(p, e, mode & S_IRUSR ? 'r' : '-');
	p = fmtchar(p, e, mode & S_IWUSR ? 'w' : '-');
	p = fmtchar(p, e, mode & S_IXUSR ? 'x' : '-');

	p = fmtchar(p, e, mode & S_IRGRP ? 'r' : '-');
	p = fmtchar(p, e, mode & S_IWGRP ? 'w' : '-');
	p = fmtchar(p, e, mode & S_IXGRP ? 'x' : '-');

	p = fmtchar(p, e, mode & S_IROTH ? 'r' : '-');
	p = fmtchar(p, e, mode & S_IWOTH ? 'w' : '-');
	p = fmtchar(p, e, mode & S_IXOTH ? 'x' : '-');

	return p;
}

static char* fmt_owner(char* p, char* e, struct ent* en, CTX)
{
	int uidlen = ctx->uidlen;
	int gidlen = ctx->gidlen;

	p = fmtpad(p, e, uidlen, fmt_uid(p, e, en->uid, ctx));
	p = fmtstr(p, e, ":");
	p = fmtpadr(p, e, gidlen, fmt_gid(p, e, en->gid, ctx));

	return p;
}

static void output(CTX, char* buf, int len)
{
	bufout(&ctx->bo, buf, len);
}

static void dump_stat_info(CTX, struct ent* en)
{
	int opts = ctx->opts;
	uint64_t size = en->size;
	int sizelen = ctx->sizelen;

	FMTBUF(p, e, buf, 100);

	if(opts & OPT_l) {
		p = fmt_mode(p, e, en);
		p = fmtstr(p, e, "  ");
		p = fmt_owner(p, e, en, ctx);
		p = fmtstr(p, e, "  ");
	}

	if(opts & OPT_n)
		p = fmtpad(p, e, sizelen, fmtu64(p, e, size));
	else
		p = fmtpad(p, e, sizelen, fmtsize(p, e, size));

	p = fmtstr(p, e, "  ");

	FMTEND(p, e);

	output(ctx, buf, p - buf);
}

static void dump_file_name(CTX, struct ent* en)
{
	int color = !(ctx->opts & OPT_c);
	int type = en->type;
	int isexe = (en->mode & 0111);
	int isdir = (type == DT_LNK_DIR || type == DT_DIR);

	if(!color)
		;
	else if(isdir)
		output(ctx, "\033[1;37m", 7);
	else if(isexe)
		output(ctx, "\033[32m", 5);
	else
		color = 0;

	output(ctx, en->name, en->namelen);

	if(isdir)
		output(ctx, "/", 1);
	if(color)
		output(ctx, "\033[0m", 4);
}

static void check_term_output(CTX)
{
	struct winsize ws;

	if(sys_ioctl(STDOUT, TIOCGWINSZ, &ws) < 0)
		ctx->opts |= OPT_c;
}

static void dump_indexed(CTX)
{
	struct bufout* bo = &(ctx->bo);
	struct ent** p = ctx->idx;

	check_term_output(ctx);
	set_max_size_len(ctx);

	for(; *p; p++) {
		struct ent* de = *p;

		dump_stat_info(ctx, de);
		dump_file_name(ctx, de);

		bufout(bo, "\n", 1);
	}
}

static void list_directory(CTX)
{
	void* ents = ctx->ptr;
	read_whole(ctx);
	void* eend = ctx->ptr;

	index_entries(ctx, ents, eend);
	sort_indexed(ctx);
	dump_indexed(ctx);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	int opts = 0, i = 1;

	memzero(ctx, sizeof(*ctx));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	ctx->npatt = argc - i;
	ctx->patt = argv + i;

	init_context(ctx, opts);
	list_directory(ctx);
	fini_context(ctx);

	return 0;
}
