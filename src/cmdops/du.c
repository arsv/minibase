#include <sys/lstat.h>
#include <sys/getdents.h>
#include <sys/open.h>
#include <sys/brk.h>
#include <sys/close.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <fail.h>

#define PAGE 4096

#define OPTS "scbndai"
#define OPT_s (1<<0)	/* size individual dirents */
#define OPT_c (1<<1)	/* show total size */
#define OPT_b (1<<2)	/* -an */
#define OPT_n (1<<3)	/* show raw byte values, w/o KMG suffix */
#define OPT_d (1<<4)	/* directories only */
#define OPT_a (1<<5)	/* count apparent size */
#define OPT_i (1<<6)	/* in given directories */

#define SET_cwd (1<<8)

ERRTAG = "du";
ERRLIST = {
	REPORT(EACCES), REPORT(EFBIG), REPORT(EINTR), REPORT(EINVAL),
	REPORT(EISDIR), REPORT(ENOTDIR), REPORT(ELOOP), REPORT(EMFILE),
	REPORT(ENFILE), REPORT(ENOMEM), REPORT(ENODEV), REPORT(EPERM),
	REPORT(EBADF), RESTASNUMBERS
};

struct entsize {
	uint64_t size;
	char* name;
};

struct heap {
	char* brk;
	char* end;
	char* ptr;

	int count;
	char** index;
};

struct node {
	int len;
	char name[];
};

static void addstsize(uint64_t* sum, struct stat* st, int opts)
{
	if(opts & OPT_a)
		*sum += st->st_size;
	else
		*sum += st->st_blocks*512;
}

/* No buffering here. For each line written, there will be at least one
   stat() call, probably much more, and the user would probably like to
   see some progress while the disc is being scanned. */

static void dump(uint64_t count, char* tag, int opts)
{
	int taglen = tag ? strlen(tag) : 0;

	char buf[100 + taglen];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;
	
	if(opts & OPT_n)
		p = fmtpad(p, e, 8, fmtu64(p, e, count));
	else
		p = fmtpad(p, e, 5, fmtsize(p, e, count));

	if(tag) {
		p = fmtstr(p, e, "  ");
		p = fmtstr(p, e, tag);
	};

	*p++ = '\n';
	
	char* q = buf;
	if(!(opts & OPT_s))
		while(*q == ' ') q++;

	writeout(q, p - q);
}

static inline int dotddot(const char* p)
{
	if(!p[0])
		return 1;
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

typedef void (*scanner)(char* dir, char* name, void* ptr, int arg);

static void for_each_in(char* path, scanner sc, void* ptr, int arg);

static void scan_dent(char* path, char* name, void* arg, int opts)
{
	uint64_t* size = arg;

	int pathlen = strlen(path);
	int namelen = strlen(name);
	char fullname[pathlen + namelen + 2];

	char* p = fullname;
	memcpy(p, path, pathlen); p += pathlen; *p++ = '/';
	memcpy(p, name, namelen); p += namelen; *p = '\0';

	struct stat st;

	long ret = syslstat(fullname, &st);
	if(ret < 0)
		return;

	addstsize(size, &st, opts);

	if((st.st_mode & S_IFMT) != S_IFDIR)
		return;

	for_each_in(fullname, scan_dent, size, opts);
}

static void for_each_in(char* path, scanner sc, void* scptr, int scarg)
{
	char debuf[PAGE];

	long fd = sysopen(path, O_RDONLY | O_DIRECTORY);

	if(fd < 0)
		fail("cannot open", path, fd);

	long rd;

	while((rd = sysgetdents64(fd, debuf, sizeof(debuf))) > 0) {
		char* ptr = debuf;
		char* end = debuf + rd;
		while(ptr < end) {
			struct dirent64* de = (struct dirent64*) ptr;
			ptr += de->reclen;

			if(!de->reclen)
				break;
			if(dotddot(de->name))
				continue;

			sc(path, de->name, scptr, scarg);
		}
	}

	sysclose(fd);
}

static int scan_path(uint64_t* size, char* path, int opts)
{
	struct stat st;

	xchk(syslstat(path, &st), "cannot stat", path);

	int isdir = ((st.st_mode & S_IFMT) == S_IFDIR);

	if((opts & OPT_d) && !isdir)
		return -1;

	addstsize(size, &st, opts);

	if(!isdir)
		return 0;
	
	for_each_in(path, scan_dent, size, opts);

	return 0;
}

static int sizecmp(const struct entsize* a, const struct entsize* b)
{
	if(a->size < b->size)
		return -1;
	if(a->size > b->size)
		return  1;
	else
		return strcmp(a->name, b->name);
}

static void scan_each(uint64_t* total, int argc, char** argv, int opts)
{
	int i;
	int n = 0;
	struct entsize res[argc];

	for(i = 0; i < argc; i++) {
		uint64_t size = 0;

		if(scan_path(&size, argv[i], opts))
			continue;

		res[n].name = argv[i];
		res[n].size = size;
		*total += size;

		n++;
	}

	if(!(opts & OPT_s))
		return;

	qsort(res, n, sizeof(*res), (qcmp)sizecmp, 0);

	for(i = 0; i < n; i++)
		dump(res[i].size, res[i].name, opts);
}

void init_heap(struct heap* ctx, int size)
{
	ctx->brk = (char*)sysbrk(0);
	ctx->end = (char*)sysbrk(ctx->brk + size);
	ctx->ptr = ctx->brk;

	if(ctx->end <= ctx->brk)
		fail("cannot init heap", NULL, 0);

	ctx->count = 0;
	ctx->index = NULL;
}

void* alloc(struct heap* ctx, int len)
{
	char* ptr = ctx->ptr;
	long avail = ctx->end - ptr;

	if(avail < len) {
		long need = len - avail;
		need += (PAGE - need % PAGE) % PAGE;
		char* req = ctx->end + need;
		char* new = (char*)sysbrk(req);

		if(new < req)
			fail("cannot allocate memory", NULL, 0);

		ctx->end = new;
	}

	ctx->ptr += len;

	return ptr;
}

static void note_dent(char* dir, char* ent, void* ptr, int opts)
{
	struct heap* ctx = ptr;

	int dirlen = strlen(dir);
	int entlen = strlen(ent);
	int usedir = !(opts & SET_cwd);

	int namelen = usedir ? dirlen + entlen + 2 : entlen + 1;
	struct node* nd = alloc(ctx, sizeof(struct node) + namelen);

	nd->len = sizeof(struct node) + namelen;

	char* p = nd->name;
	char* e = nd->name + namelen - 1;

	if(usedir) {
		p = fmtstr(p, e, dir);
		p = fmtchar(p, e, '/');
	}
	p = fmtstr(p, e, ent);
	*p++ = '\0';

	ctx->count++;
}

static void index_notes(struct heap* ctx)
{
	char* ptr = ctx->brk;
	char* end = ctx->end;
	int n = ctx->count;
	int i = 0;

	char** index = alloc(ctx, n*sizeof(char*));

	while(ptr < end && i < n) {
		struct node* nd = (struct node*) ptr;
		index[i++] = nd->name;
		ptr += nd->len;
	}

	ctx->index = index;
}

static void scan_dirs(uint64_t* total, int argc, char** argv, int opts)
{
	struct heap ctx;
	int i;

	init_heap(&ctx, 2*PAGE);

	for(i = 0; i < argc; i++)
		for_each_in(argv[i], note_dent, &ctx, opts);

	index_notes(&ctx);

	scan_each(total, ctx.count, ctx.index, opts);
}

static void scan_cwd(uint64_t* total, int opts)
{
	char* list[] = { "." };

	scan_dirs(total, 1, list, opts | SET_cwd);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	argc -= i;
	argv += i;

	uint64_t total = 0;

	if(opts & OPT_b)
		opts |= OPT_a | OPT_n;

	if(opts & (OPT_s | OPT_c))
		;
	else if(argc == 1 && !(opts & OPT_i))
		opts |= OPT_c;
	else
		opts |= OPT_s;

	if(opts & OPT_i)
		scan_dirs(&total, argc, argv, opts);
	else if(argc)
		scan_each(&total, argc, argv, opts);
	else 
		scan_cwd(&total, opts);

	if(opts & OPT_c)
		dump(total, NULL, opts);

	flushout();

	return 0;
}
