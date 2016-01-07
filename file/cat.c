#include <bits/errno.h>
#include <bits/fcntl.h>
#include <sys/open.h>
#include <sys/read.h>
#include <sys/write.h>

#include <fail.h>
#include <null.h>
#include <xchk.h>

static const int CATBUF = 8000;

ERRTAG = "cat";
ERRLIST = {
	REPORT(EAGAIN), REPORT(EBADF), REPORT(EFAULT), REPORT(EINTR),
	REPORT(EINVAL), REPORT(EIO), REPORT(EISDIR), REPORT(EDQUOT),
	REPORT(EFBIG), REPORT(ENOSPC), REPORT(EPERM), REPORT(EPIPE),
	RESTASNUMBERS
};

static void dump(const char* buf, int len)
{
	long wr;

	while(len > 0)
		if((wr = syswrite(1, buf, len)) > 0) {
			buf += wr;
			len -= wr;
		} else {
			/* wr = 0 is probably just as bad as wr < 0 here */
			fail("write", NULL, -wr);
		}
}

static void cat(const char* name, int fd)
{
	char buf[CATBUF];
	long rd;
	
	while((rd = xchk(sysread(fd, buf, CATBUF), "read", name)))
		dump(buf, rd);
}

static int xopen(const char* name)
{
	return xchk(sysopen(name, O_RDONLY), NULL, name);
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
