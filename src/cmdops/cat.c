#include <sys/file.h>
#include <errtag.h>
#include <util.h>

ERRTAG("cat");
ERRLIST(NEAGAIN NEBADF NEFAULT NEINTR NEINVAL NEIO NEISDIR NEDQUOT
	NEFBIG NENOSPC NEPERM NEPIPE);

static char catbuf[256*4096]; /* 1MB */

static void dump(const char* buf, int len)
{
	long wr;

	while(len > 0)
		if((wr = sys_write(1, buf, len)) > 0) {
			buf += wr;
			len -= wr;
		} else {
			/* wr = 0 is probably just as bad as wr < 0 here */
			fail("write", NULL, wr);
		}
}

static void cat(const char* name, int fd)
{
	long rd;
	
	while((rd = xchk(sys_read(fd, catbuf, sizeof(catbuf)), "read", name)))
		dump(catbuf, rd);
}

static int xopen(const char* name)
{
	return xchk(sys_open(name, O_RDONLY), NULL, name);
}

int main(int argc, const char** argv)
{
	int i;

	if(argc < 2)
		cat("stdin", 0);
	else for(i = 1; i < argc; i++)
		cat(argv[i], xopen(argv[i]));
	return 0;
}
