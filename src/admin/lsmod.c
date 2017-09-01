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

	char* brk = (char*)sys_brk(0);
	char* end = (char*)sys_brk(brk + 4*PAGE);

	if(end <= brk) nomem();

	long rd;
	char* ptr = brk;

	while((rd = sys_read(fd, ptr, end - ptr)) > 0) {
		ptr += rd;
		if(ptr < end) continue;
		end = (char*)sys_brk(end + 2*PAGE);
		if(ptr >= end) nomem();
	} if(rd < 0) {
		fail("read", fname, rd);
	}

	mods->ptr = brk;
	mods->end = ptr;

	return brk;
}

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

/* A line from /proc/modules looks like this:

     scsi_mod 147456 4 uas,usb_storage,sd_mod,libata, Live 0xffffffffa0217000

   We only need fields #0 module name and #3 used-by, so that our output
   would be

     scsi_mod (uas,usb_storage,sd_mod,libata)

   Non-empty used-by list has a trailing comma, and empty list is "-". */

static void list_mods(struct strptr* mods)
{
	int n;

	char* buf = mods->ptr;
	char* end = mods->end;

	char* ls;
	char* le;

	for(ls = buf; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');
		struct strptr parts[5];

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
