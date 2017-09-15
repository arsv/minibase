#include <sys/file.h>
#include <sys/mman.h>

#include <errtag.h>
#include <string.h>
#include <output.h>
#include <util.h>

ERRTAG("lsmod");
ERRLIST(NENOENT NENOMEM NEACCES NEPERM NEFAULT NEINTR NEINVAL NEISDIR
	NELOOP NEMFILE NENFILE NENOTDIR);

struct strptr {
	char* ptr;
	char* end;
};

struct lineidx {
	int n;
	char** s;
};

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

static int cmp(const void* a, const void* b, long _)
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

	qsort(idx, n, sizeof(char*), cmp, 0);

	lx->s = idx;
	lx->n = n;
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

static void writestr(struct strptr* str)
{
	writeout(str->ptr, strplen(str));
}

static void list_mods(struct strptr* mods)
{
	struct lineidx lx;

	index_lines(&lx, mods);

	for(int i = 0; i < lx.n; i++) {
		char* ls = lx.s[i];
		char* le = lx.s[i+1];
		struct strptr parts[5];
		int n;

		if((n = split(ls, le, parts, 5) < 4))
			continue;

		writestr(&parts[0]);

		if(strpeq(&parts[2], "0"))
			goto nl;
		if(strpeq(&parts[4], "-"))
			goto nl;
		if(strplen(&parts[3]) <= 1)
			goto nl;

		writeout(" (", 2);
		parts[3].end--; /* skip trailing comma */
		writestr(&parts[3]);
		writeout(")", 1);
	nl:
		writeout("\n", 1);
	}

	flushout();
}

int main(void)
{
	struct strptr modlist;

	read_whole(&modlist, "/proc/modules");
	list_mods(&modlist);

	return 0;
}
