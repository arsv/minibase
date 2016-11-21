#include <sys/mknod.h>

#include <format.h>
#include <fail.h>

#define makedev(x,y) ( \
        (((x)&0xfffff000ULL) << 32) | \
	(((x)&0x00000fffULL) << 8) | \
        (((y)&0xffffff00ULL) << 12) | \
	(((y)&0x000000ffULL)) )

ERRTAG = "mknod";
ERRLIST = {
	REPORT(EACCES), REPORT(EDQUOT), REPORT(EEXIST), REPORT(EFAULT),
	REPORT(EINVAL), REPORT(ELOOP), REPORT(ENOENT), REPORT(ENOMEM),
	REPORT(ENOSPC), REPORT(ENOTDIR), REPORT(EPERM), REPORT(EROFS),
	RESTASNUMBERS
};

static int parsemode(const char* mode)
{
	const char* p;
	int d, m = 0;

	if(*mode++ != '0')
		fail("mode must be octal", NULL, 0);

	for(p = mode; *p; p++)
		if(*p >= '0' && (d = *p - '0') < 8)
			m = (m<<3) | d;
		else
			fail("bad mode specification", mode, 0);

	return m;
}

static int parsetype(const char* type)
{
	if(!type[0] || type[1])
		goto bad;

	switch(*type) {
		case 'c':
		case 'u': return S_IFCHR;
		case 'b': return S_IFBLK;
		case 'p': return S_IFIFO;
		case 'r': return S_IFREG;
		case 's': return S_IFSOCK;
	}

bad:	fail("bad node type specification", NULL, 0);
}

int xparseint(char* str)
{
	int n;

	char* end = parseint(str, &n);
	if(*end || end == str)
		fail("not a number", str, 0);

	return n;
}

int main(int argc, char** argv)
{
	int i = 1;
	int mode = 0666;
	char* name = NULL;
	int type = 0;
	int major = 0;
	int minor = 0;

	if(argc < 3)
		fail("file name and type must be supplied", NULL, 0);

	if(i < argc)
		name = argv[i++];
	if(i < argc && argv[i][0] >= '0' && argv[i][0] <= '9')
		mode = parsemode(argv[i++]);
	if(i < argc)
		type = parsetype(argv[i++]);

	int isdev = (type == S_IFCHR || type == S_IFBLK);

	if(isdev && (i != argc - 2))
		fail("major and minor numbers must be supplied", NULL, 0);
	if(!isdev && (i != argc))
		fail("too many arguments", NULL, 0);

	if(isdev) {
		major = xparseint(argv[i++]);
		minor = xparseint(argv[i++]);
	}

	long dev = makedev(major, minor);
	long ret = sysmknod(name, mode | type, dev);

	if(ret < 0)
		fail("cannot create", name, ret);

	return 0;
}
