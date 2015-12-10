#include <bits/errno.h>
#include <bits/fcntl.h>
#include <sys/open.h>
#include <sys/read.h>
#include <sys/write.h>

#include <fail.h>
#include <null.h>

static const char* TAG = "cat";
static const int CATBUF = 8000;

ERRLIST = {
	REPORT(EAGAIN), REPORT(EBADF), REPORT(EFAULT), REPORT(EINTR),
	REPORT(EINVAL), REPORT(EIO), REPORT(EISDIR), REPORT(EDQUOT),
	REPORT(EFBIG), REPORT(ENOSPC), REPORT(EPERM), REPORT(EPIPE),
	RESTASNUMBERS
};

static int xopen(const char* name)
{
	long ret = sysopen(name, O_RDONLY);
	if(ret < 0)
		fail(TAG, NULL, name, -ret);
	return (int)ret;
}

static void dump(const char* buf, int len)
{
	long wr;

	while(len > 0)
		if((wr = syswrite(1, buf, len)) > 0) {
			buf += wr;
			len -= wr;
		} else {
			fail(TAG, "write failed", NULL, -wr);
		}
}

static void cat(const char* name, int fd)
{
	char buf[CATBUF];
	long rd;
	
	while((rd = sysread(fd, buf, CATBUF)) > 0)
		dump(buf, rd);
	if(rd < 0)
		fail(TAG, "cannot read from", name, -rd);
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
