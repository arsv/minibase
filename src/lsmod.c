#include <sys/open.h>
#include <sys/read.h>
#include <sys/brk.h>

#include <fail.h>
#include <strlen.h>
#include <strecbrk.h>
#include <writeout.h>

#define PAGE 4096

ERRTAG = "lsmod";
ERRLIST = {
	REPORT(ENOENT), REPORT(ENOMEM), REPORT(EACCES), REPORT(EPERM),
	REPORT(EFAULT), REPORT(EINTR), REPORT(EINVAL), REPORT(EISDIR),
	REPORT(ELOOP), REPORT(EMFILE), REPORT(ENFILE), REPORT(ENOTDIR),
	RESTASNUMBERS
};

static char* extend(char* ptr, long len)
{
	return (char*)xchk(sysbrk(ptr + len), "brk", NULL);
}

/* Files in /proc have zero size, so there's no way to tell how
   large the module list is without reading it. The idea here
   is to provide a buffer large enough for most reasonable cases
   to have a single read() call, with subsequent brk() being
   a kind of undesired fallback scenario.
 
   Moderately bloated Intel laptop running 4.2.5 has /proc/modules
   about 6KB large, so take 16KB as an upper estimate for sane systems. */

static char* readwhole(const char* fname, char** outend)
{
	long fd = xchk(sysopen(fname, O_RDONLY), "cannot open", fname);

	char* brk = extend(NULL, 0);
	char* end = extend(brk, 4*PAGE);

	long rd;
	char* ptr = brk;
	while((rd = sysread(fd, ptr, end - ptr)) > 0) {
		ptr += rd;
		if(ptr >= end)
			end = extend(end, 2*PAGE);
	} if(rd < 0) {
		fail("read", fname, rd);
	}

	*outend = ptr;
	return brk;
}

struct strptr {
	char* ptr;
	long len;
};

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
		parts[i].len = pe - ps;
		i++;

		for(ps = pe; ps < end && isspace(*ps); ps++) ;
	}

	return i;
}

/* A line from /proc/modules looks like this:

     scsi_mod 147456 4 uas,usb_storage,sd_mod,libata, Live 0xffffffffa0217000

   We only need fields #0 module name, #2 refcnt, #3 used-by, so that
   our output would be

     scsi_mod         4 uas,usb_storage,sd_mod,libata

   Module name gets padded to align refcnt column, and the rest is used as is.

   Non-empty used-by list has a trailing comma, and empty list is "-". */

static void listmods(char* modules, char* end)
{
	char* ls;
	char* le;
	const char* pad = "                    ";
	const int padlen = strlen(pad);

	for(ls = modules; ls < end; ls = le + 1) {
		le = strecbrk(ls, end, '\n');
		struct strptr parts[5];
		int n = split(ls, le, parts, 5);

		if(n < 4) continue;

		writeout(parts[0].ptr, parts[0].len);
		if(parts[0].len < padlen)
			writeout((char*)pad, padlen - parts[0].len);
		writeout(" ", 1);
		writeout(parts[2].ptr, parts[2].len);

		if(parts[3].len > 1) {
			writeout(" ", 1);
			writeout(parts[3].ptr, parts[3].len - 1);
		}

		writeout("\n", 1);
	}

	flushout();
}

int main(void)
{
	char* end = NULL;
	char* modules = readwhole("/proc/modules", &end);

	listmods(modules, end);

	return 0;
}
