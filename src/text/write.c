#include <sys/open.h>
#include <sys/read.h>
#include <sys/write.h>

#include <fail.h>
#include <null.h>
#include <alloca.h>
#include <fmtstr.h>
#include <strlen.h>
#include <argsmerge.h>

ERRTAG = "write";
ERRLIST = {
	REPORT(EAGAIN), REPORT(EBADF), REPORT(EFAULT), REPORT(EINTR),
	REPORT(EINVAL), REPORT(EIO), REPORT(EISDIR), REPORT(EDQUOT),
	REPORT(EFBIG), REPORT(ENOSPC), REPORT(EPERM), REPORT(EPIPE),
	REPORT(EEXIST), RESTASNUMBERS
};

static int parsemode(const char* mode)
{
	const char* p;
	int d, m = 0;

	for(p = mode; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 8)
			m = (m<<3) | d;
		else
			fail("bad mode specification", mode, 0);

	return m;
}

static int xopen(const char* name, int flags, int mode)
{
	long ret = sysopen3(name, flags, mode);
	if(ret < 0)
		fail("cannot open", name, -ret);
	return (int)ret;
}

static void writeall(int fd, const char* buf, int len)
{
	long wr;

	while(len > 0)
		if((wr = syswrite(fd, buf, len)) > 0) {
			buf += wr;
			len -= wr;
		} else {
			fail(NULL, NULL, -wr);
		}
}

static void writeargs(int fd, int argc, char** argv)
{
	int len = argsumlen(argc, argv) + argc;
	char* buf = alloca(len + 2);
	char* end = buf + sizeof(buf);

	argsmerge(buf, end, argc, argv);
	writeall(fd, buf, end - buf);
}

int main(int argc, char** argv)
{
	int i = 1;
	int flags = O_WRONLY;
	int mode = 0666;
	char opt[] = "-?";
	const char* p;

	if(i < argc && argv[i][0] == '-')
		for(p = argv[i++] + 1; *p; p++) switch(*p) {
			case 'c': flags |= O_CREAT; break;
			case 'a': flags |= O_APPEND; break;
			case 'x': flags |= O_CREAT | O_EXCL; break;
			case 't': flags |= O_NOATIME; break;
			case 'l': flags |= O_NOFOLLOW; break;
			case 'y': flags |= O_SYNC; break;
			case 'm': mode = 0; break;
			default: opt[1] = *p;
				 fail("unknown option", opt, 0);
		}
	if(!(flags & O_APPEND))
		flags |= O_TRUNC;

	if(i < argc && !mode)
		mode = parsemode(argv[i++]);

	if(i >= argc)
		fail("need a file to work on", NULL, 0);

	int fd = xopen(argv[i++], flags, mode);

	argc -= i;
	argv += i;

	writeargs(fd, argc, argv);

	return 0;
}
