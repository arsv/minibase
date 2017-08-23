#include <sys/file.h>
#include <sys/mman.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <fail.h>

/* This tool expects the search to return much less than complete subtree
   list, and preferably with fast output start. This sets it apart from
   some kind of ls -r | grep combo, which otherwise would have been
   an easier approach.
 
   Sorting is tricky here, but non-sorted output is non-reproducible
   so let's take sorting as the lesser evil.

   Having DT_UNKNOWN in getdents output reduces efficiency a lot,
   as we have to stat() all entries, but the only way around it
   is to drop sorting. */

ERRTAG = "ff";
ERRLIST = {
	REPORT(ENOENT), REPORT(EISDIR), REPORT(ENOTDIR), REPORT(EFAULT),
	REPORT(ENOMEM), REPORT(EINVAL), REPORT(EBADF), REPORT(EPERM),
	REPORT(EACCES), RESTASNUMBERS
};

/* Algo overview: make a mixed list of matching entries and all subdirs
   in a given directory

       entries(patt, d) = sort { namecmp }
                          filter { isdir(_) or matches(_, patt) }
                          map { "$d/$_" } readdir(d)

   then traverse the list in order, recursing into subdirs but taking
   non-dir entries as is

       check(patt, _) = isdir(_) ? output(patt, _) : [ _ ]

       output(patt, d) = concat map { check(patt, _) } entries(patt, d)

   This gives one-dir limit on sort input and still guarantees correct
   output, because anything under a given dir shares the same prefix
   and will get sorted exactly where the dir itself would be. */

#define PAGE 4096

#define OPTS "i"
#define OPT_i (1<<0)

#define MFILE 0
#define MDIR 1

struct shortent {
	short len;
	char dir;
	char name[];
};

struct pattern {
	char* name;
	int len;
	int where;
};

struct topctx {
	int opts;
	void* ptr;
	void* brk;
	struct pattern* patt;
	int pcnt;
};

struct dirctx {
	struct topctx* tc;
	char* dir;
	int count;
	struct shortent* ents;
	struct shortent** idx;
};

static char dirbuf[2*PAGE];

static void* setbrk(void* old, int incr)
{
	long addr = sys_brk(old + incr);

	if(addr < 0 && addr >= -2048)
		fail("brk", NULL, addr);
	if(old && (addr == (long)old))
		fail("out of memory", NULL, 0);

	return (void*)addr;
}

static void* extend(struct topctx* tc, int size)
{
	if(tc->brk - tc->ptr < size) {
		int aligned = size + (PAGE - size % PAGE);
		tc->brk = setbrk(tc->brk, aligned);
	}

	void* ret = tc->ptr;
	tc->ptr += size;
	return ret;
}

static int pathsize(struct dirctx* dc, char* name)
{
	if(dc->dir)
		return strlen(dc->dir) + strlen(name) + 2;
	else
		return strlen(name) + 1;
}

static void makepath(struct dirctx* dc, char* name, char* buf, int len)
{
	char* p = buf;
	char* e = p + len - 1;

	if(dc->dir) {
		p = fmtstr(p, e, dc->dir);
		p = fmtchar(p, e, '/');
	}

	p = fmtstr(p, e, name);
	*p++ = '\0';
}

static void enqueue(struct dirctx* dc, char* name, int isdir)
{
	struct topctx* tc = dc->tc;

	int pathlen = pathsize(dc, name);
	int size = sizeof(struct shortent) + pathlen;

	struct shortent* p = (struct shortent*) extend(tc, size);

	p->dir = isdir;
	p->len = size;

	makepath(dc, name, p->name, pathlen);

	dc->count++;
}

static void checkfile(struct dirctx* dc, char* name)
{
	struct topctx* tc = dc->tc;
	int namelen = strlen(name);
	int i;

	for(i = 0; i < tc->pcnt; i++) {
		struct pattern* p = &tc->patt[i];

		if(p->len > namelen)
			continue;

		char* anchor = p->where ? name + namelen - p->len : name;

		if(!strncmp(anchor, p->name, p->len))
			break;
	}
	
	if(i >= tc->pcnt)
		return;

	enqueue(dc, name, MFILE);
}

static void checkstat(struct dirctx* dc, char* name)
{
	fail("not implemented", NULL, 0);
}

static void checkdent(struct dirctx* dc, struct dirent* de)
{
	int type = de->type;
	char* name = de->name;

	if(type == DT_DIR)
		enqueue(dc, name, MDIR);
	else if(type == DT_UNKNOWN)
		checkstat(dc, name);
	else
		checkfile(dc, name);
}

static int dotddot(const char* name)
{
	if(name[0] != '.') return 0;
	if(name[1] == '\0') return 1;
	if(name[1] != '.') return 0;
	if(name[2] == '\0') return 1;
	return 0;
}

static void readscan(struct dirctx* dc, int fd)
{
	char* dir = dc->dir;

	struct dirent64* debuf = (struct dirent64*) dirbuf;
	long len = sizeof(dirbuf);
	long ret;

	dc->ents = (struct shortent*) dc->tc->ptr;

	while((ret = sys_getdents(fd, debuf, len)) > 0) {
		void* ptr = (void*) debuf;
		void* end = ptr + ret;
		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(!dotddot(de->name))
				checkdent(dc, de);
			if(de->reclen <= 0)
				break;
			ptr += de->reclen;
		}
	} if(ret < 0)
		fail("cannot read entries from", dir, ret);
}

struct shortent* nextshortent(struct shortent* p)
{
	void* q = (void*) p;
	return (struct shortent*)(q + p->len);
}

static int cmpidx(struct shortent** a, struct shortent** b, int opts)
{
	return strcmp((*a)->name, (*b)->name);
}

static void idxfound(struct dirctx* dc)
{
	struct topctx* tc = dc->tc;

	int nents = dc->count;
	int size = nents * sizeof(void*);
	dc->idx = (struct shortent**) extend(tc, size);

	struct shortent* p = dc->ents;
	int i;

	for(i = 0; i < nents; i++) {
		dc->idx[i] = (struct shortent*) p;
		p = nextshortent(p);
	}

	qsort(dc->idx, nents, sizeof(void*), (qcmp)cmpidx, tc->opts);
}

static void searchdir(struct topctx* tc, char* dir);

static void printrec(struct dirctx* dc)
{
	int i;

	for(i = 0; i < dc->count; i++) {
		struct shortent* q = dc->idx[i];

		if(q->dir) {
			searchdir(dc->tc, q->name);
		} else {
			writeout(q->name, strlen(q->name));
			writeout("\n", 1);
		}
	}
}

static void searchdir(struct topctx* tc, char* dir)
{
	void* saved = tc->ptr;
	char* dirname = dir ? dir : ".";

	long fd = sys_open(dirname, O_DIRECTORY);

	if(fd < 0) return;

	struct dirctx dc = {
		.tc = tc,
		.dir = dir,
		.count = 0,
		.ents = NULL,
		.idx = NULL
	};

	readscan(&dc, fd);
	sys_close(fd);

	idxfound(&dc);
	printrec(&dc);

	tc->ptr = saved;
}

static void setpatterns(struct topctx* tc, int argc, char** argv)
{
	int i;

	for(i = 0; i < argc; i++) {
		char* argi = argv[i];
		struct pattern* p = &tc->patt[i];
		p->name = argi;
		p->len = strlen(argi);
		p->where = (*argi == '.');
	}
}

int main(int argc, char** argv)
{
	int opts = 0;
	int i = 1;
	char* start = NULL;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts & OPT_i)
		start = argv[i++];

	if(i >= argc)
		fail("need names to search", NULL, 0);

	argc -= i;
	argv += i;

	struct topctx tc;
	struct pattern patt[argc];

	tc.opts = opts;
	tc.ptr = setbrk(NULL, 0);
	tc.brk = setbrk(tc.ptr, PAGE);
	tc.patt = patt;
	tc.pcnt = argc;

	setpatterns(&tc, argc, argv);

	searchdir(&tc, start);

	flushout();

	return 0;
}
