#include <sys/file.h>
#include <sys/mman.h>
#include <sys/dents.h>

#include <string.h>
#include <format.h>
#include <output.h>
#include <errtag.h>
#include <util.h>

/* This tool expects the search to return much less than complete subtree
   list, and preferably with fast output start. This sets it apart from
   some kind of ls -r | grep combo, which otherwise would have been
   an easier approach.
 
   Sorting is tricky here, but non-sorted output is non-reproducible
   so let's take sorting as the lesser evil.

   Having DT_UNKNOWN in getdents output reduces efficiency a lot,
   as we have to stat() all entries, but the only way around it
   is to drop sorting. */

ERRTAG("ff");

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
	short pre;
	char isdir;
	char name[];
};

struct pattern {
	char* name;
	int len;
	int where;
};

struct top {
	int opts;
	void* ptr;
	void* brk;
	struct pattern* patt;
	int pcnt;
	struct bufout bo;
};

struct dir {
	struct top* tc;
	int fd;
	char* dir;
	int count;
	struct shortent* ents;
	struct shortent** idx;
};

#define TC struct top* tc
#define DC struct dir* dc

static char dirbuf[2*PAGE];
static char outbuf[PAGE];

static void init_output(TC)
{
	struct bufout* bo = &tc->bo;

	bo->fd = STDOUT;
	bo->buf = outbuf;
	bo->ptr = 0;
	bo->len = sizeof(outbuf);
}

static void fini_outout(TC)
{
	bufoutflush(&tc->bo);
}

static void outstrnl(TC, char* str)
{
	struct bufout* bo = &tc->bo;

	bufout(bo, str, strlen(str));
	bufout(bo, "\n", 1);
}

static void* setbrk(void* old, int incr)
{
	void* ptr = sys_brk(old + incr);

	if(brk_error(old, ptr))
		fail("out of memory", NULL, 0);

	return ptr;
}

static void* extend(TC, int size)
{
	if(tc->brk - tc->ptr < size) {
		int aligned = size + (PAGE - size % PAGE);
		tc->brk = setbrk(tc->brk, aligned);
	}

	void* ret = tc->ptr;
	tc->ptr += size;
	return ret;
}

static int pathsize(DC, char* name)
{
	if(dc->dir)
		return strlen(dc->dir) + strlen(name) + 2;
	else
		return strlen(name) + 1;
}

static void enqueue(DC, char* name, int isdir)
{
	struct top* tc = dc->tc;

	int pathlen = pathsize(dc, name);
	int size = sizeof(struct shortent) + pathlen;

	struct shortent* se = extend(tc, size);

	se->isdir = isdir;
	se->len = size;

	char* p = se->name;
	char* e = p + pathlen - 1;

	if(dc->dir) {
		p = fmtstr(p, e, dc->dir);
		p = fmtstr(p, e, "/");
		se->pre = p - se->name;
	} else {
		se->pre = 0;
	}

	p = fmtstr(p, e, name);
	*p++ = '\0';

	dc->count++;
}

static void check_file(DC, char* name)
{
	struct top* tc = dc->tc;
	int namelen = strlen(name);
	int i;

	for(i = 0; i < tc->pcnt; i++) {
		struct pattern* p = &tc->patt[i];

		if(p->len > namelen)
			continue;

		char* anchor = p->where ? name + namelen - p->len : name;

		if(strncmp(anchor, p->name, p->len))
			continue;

		enqueue(dc, name, MFILE);
		break;
	}
}

static void check_stat(DC, char* name)
{
	(void)dc;
	(void)name;

	fail("not implemented", NULL, 0);
}

static void check_dent(DC, struct dirent* de)
{
	int type = de->type;
	char* name = de->name;

	if(type == DT_DIR)
		enqueue(dc, name, MDIR);
	else if(type == DT_UNKNOWN)
		check_stat(dc, name);
	else
		check_file(dc, name);
}

static void read_scan(DC, int fd)
{
	char* dir = dc->dir;
	long dblen = sizeof(dirbuf);
	long ret;

	dc->ents = (struct shortent*) dc->tc->ptr;

	while((ret = sys_getdents(fd, dirbuf, dblen)) > 0) {
		void* ptr = dirbuf;
		void* end = ptr + ret;

		while(ptr < end) {
			struct dirent* de = (struct dirent*) ptr;

			if(de->reclen <= 0)
				break;

			ptr += de->reclen;

			if(dotddot(de->name))
				continue;

			check_dent(dc, de);
		}

		if(ret < dblen - PAGE)
			return;
	}

	if(ret < 0) fail("cannot read entries from", dir, ret);
}

static int cmpidx(const void* a, const void* b)
{
	struct shortent* pa = *((struct shortent**)a);
	struct shortent* pb = *((struct shortent**)b);

	return strcmp(pa->name, pb->name);
}

struct shortent* next_shortent(struct shortent* p)
{
	void* q = (void*) p;
	return (struct shortent*)(q + p->len);
}

static void index_entries(DC)
{
	struct top* tc = dc->tc;

	int nents = dc->count;
	int size = nents * sizeof(void*);
	dc->idx = (struct shortent**) extend(tc, size);

	struct shortent* p = dc->ents;
	int i;

	for(i = 0; i < nents; i++) {
		dc->idx[i] = (struct shortent*) p;
		p = next_shortent(p);
	}

	qsort(dc->idx, nents, sizeof(void*), cmpidx);
}

static void scan_dir(TC, int at, char* openname, char* dirname);

static void print_indexed(DC)
{
	struct top* tc = dc->tc;
	int at = dc->fd;

	for(int i = 0; i < dc->count; i++) {
		struct shortent* se = dc->idx[i];

		if(se->isdir)
			scan_dir(tc, at, se->name + se->pre, se->name);
		else
			outstrnl(tc, se->name);
	}
}

static void scan_dir(TC, int at, char* openname, char* listname)
{
	int fd;
	void* ptr;

	if((fd = sys_openat(at, openname, O_DIRECTORY)) < 0)
		return;

	struct dir dc = {
		.tc = tc,
		.fd = fd,
		.dir = listname,
		.count = 0,
		.ents = NULL,
		.idx = NULL
	};

	ptr = tc->ptr;

	read_scan(&dc, fd);

	index_entries(&dc);
	print_indexed(&dc);

	sys_close(fd);

	tc->ptr = ptr;
}

static void prep_patterns(TC, int argc, char** argv)
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

	struct top context, *tc = &context;
	struct pattern patt[argc];

	memzero(tc, sizeof(*tc));

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts & OPT_i)
		start = argv[i++];

	if(i >= argc)
		fail("need names to search", NULL, 0);

	argc -= i;
	argv += i;

	tc->opts = opts;
	tc->ptr = setbrk(NULL, 0);
	tc->brk = setbrk(tc->ptr, PAGE);
	tc->patt = patt;
	tc->pcnt = argc;

	init_output(tc);
	prep_patterns(tc, argc, argv);

	if(start)
		scan_dir(tc, AT_FDCWD, start, start);
	else
		scan_dir(tc, AT_FDCWD, ".", NULL);

	fini_outout(tc);

	return 0;
}
