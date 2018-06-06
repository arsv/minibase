#include <sys/file.h>
#include <sys/mman.h>

#include <string.h>
#include <output.h>
#include <util.h>
#include <main.h>

ERRTAG("lsmod");
ERRLIST(NENOENT NENOMEM NEACCES NEPERM NEFAULT NEINTR NEINVAL NEISDIR
	NELOOP NEMFILE NENFILE NENOTDIR);

struct strptr {
	char* ptr;
	char* end;
};

struct lineidx {
	int num;
	char** idx;
	char* end;
};

struct query {
	int n;
	char** a;
};

char outbuf[PAGE];

static void nomem(void)
{
	fail("cannot allocate memory", NULL, 0);
}

/* Files in /proc have zero size, so there's no way to tell how
   large the module list is without reading it. The idea here
   is to provide a buffer large enough for most reasonable cases
   to have a single read() call, with subsequent brk() being
   a kind of undesired fallback scenario.

   Moderately bloated Intel laptop running 4.2.5 has /proc/modules
   about 6KB large, so take 16KB as an upper estimate for sane systems. */

static char* read_whole(struct strptr* mods, const char* fname)
{
	int fd;

	if((fd = sys_open(fname, O_RDONLY)) < 0)
		fail(NULL, fname, fd);

	char* brk = sys_brk(0);
	char* end = sys_brk(brk + 4*PAGE);

	if(end <= brk) nomem();

	long rd;
	char* ptr = brk;

	while((rd = sys_read(fd, ptr, end - ptr)) > 0) {
		ptr += rd;
		if(ptr < end) continue;
		end = sys_brk(end + 2*PAGE);
		if(ptr >= end) nomem();
	} if(rd < 0) {
		fail("read", fname, rd);
	}

	mods->ptr = brk;
	mods->end = ptr;

	return brk;
}

/* The lines in /proc/modules are not ordered, at least not in any
   useful sense. For the sake of general sanity, let's sort the lines.
   Most users will be looking for a particular module anyway, having
   them sorted would simplify that. */

static int count_lines(char* buf, char* end)
{
	char* p;
	int nl = 1, n = 0;

	for(p = buf; p < end; p++) {
		if(nl) { n++; nl = 0; }
		if(!*p || *p == '\n') { nl = 1; }
	}

	return n;
}

static void set_line_ptrs(char* buf, char* end, char** idx, int n)
{
	char* p;
	int nl = 1, i = 0;

	for(p = buf; p < end; p++) {
		if(nl) { idx[i++] = p; nl = 0; }
		if(!*p || *p == '\n') { *p = '\0'; nl = 1; }
		if(i >= n) break;
	}

	idx[i] = p;
}

static char** alloc_index(int n)
{
	void* brk = sys_brk(NULL);
	void* req = brk + (n+1)*sizeof(char*);
	void* end = sys_brk(req);

	if(end < req) nomem();

	return (char**)brk;
}

static int cmp(const void* a, const void* b)
{
	char* sa = *((char**)a);
	char* sb = *((char**)b);
	return strcmp(sa, sb);
}

static void index_lines(struct lineidx* lx, struct strptr* mods)
{
	char* buf = mods->ptr;
	char* end = mods->end;
	int n = count_lines(buf, end);

	char** idx = alloc_index(n);

	set_line_ptrs(buf, end, idx, n);

	qsort(idx, n, sizeof(char*), cmp);

	lx->idx = idx;
	lx->num = n;
	lx->end = end;
}

static int match_line(struct strptr* sp, struct query* mq)
{
	char* str = sp->ptr;
	uint len = sp->end - sp->ptr;
	int i, n = mq->n;

	for(i = 0; i < n; i++) {
		char* arg = mq->a[i];

		if(len < strlen(arg))
			return 0;
		if(!strnstr(str, arg, len))
			return 0;;
	}

	return 1;
}

/* A line from /proc/modules looks like this:

     scsi_mod 147456 4 uas,usb_storage,sd_mod,libata, Live 0xffffffffa0217000

   We only need fields #0 module name and #3 used-by, so that our output
   would be

     scsi_mod (uas,usb_storage,sd_mod,libata)

   Non-empty used-by list has a trailing comma, and empty list is "-". */

static int isspace(int c)
{
	return (c == ' ' || c == '\t');
}

static int split(char* str, char* end, struct strptr* parts, int n)
{
	char* ps = str;
	char* pe;
	int i = 0;

	while(ps < end && i < n) {
		for(pe = ps; pe < end && !isspace(*pe); pe++) ;

		parts[i].ptr = ps;
		parts[i].end = pe;
		i++;

		for(ps = pe; ps < end && isspace(*ps); ps++) ;
	}

	return i;
}

static int strplen(struct strptr* str)
{
	return str->end - str->ptr;
}

static int strpeq(struct strptr* str, char* a)
{
	return !strncmp(str->ptr, a, strplen(str));
}

static void outstr(struct bufout* bo, struct strptr* str)
{
	bufout(bo, str->ptr, strplen(str));
}

static void list_mods(struct bufout* bo, struct lineidx* lx, struct query* mq)
{
	char* end = lx->end;
	int num = lx->num;
	char** idx = lx->idx;

	for(int i = 0; i < num; i++) {
		char* ls = idx[i];
		char* le = strecbrk(ls, end, '\0');
		struct strptr parts[5];
		int n;

		if((n = split(ls, le, parts, 5)) < 4)
			continue;
		if(!match_line(&parts[0], mq))
			continue;

		outstr(bo, &parts[0]);

		if(strpeq(&parts[2], "0"))
			goto nl;
		if(strpeq(&parts[4], "-"))
			goto nl;
		if(strplen(&parts[3]) <= 1)
			goto nl;

		bufout(bo, " (", 2);
		parts[3].end--; /* skip trailing comma */
		outstr(bo, &parts[3]);
		bufout(bo, ")", 1);
	nl:
		bufout(bo, "\n", 1);
	}
}

int main(int argc, char** argv)
{
	struct strptr modlist;
	struct lineidx lx;
	int i = 1;

	if(i < argc && argv[i][0] == '-') {
		if(argv[i][1])
			fail("unsupported options", NULL, 0);
		else i++;
	}

	struct bufout bo = {
		.fd = STDOUT,
		.buf = outbuf,
		.ptr = 0,
		.len = sizeof(outbuf)
	};

	struct query mq = {
		.n = argc - i,
		.a = argv + i
	};

	read_whole(&modlist, "/proc/modules");
	index_lines(&lx, &modlist);
	list_mods(&bo, &lx, &mq);
	bufoutflush(&bo);

	return 0;
}
