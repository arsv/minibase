#include <bits/types.h>
#include <sys/open.h>
#include <sys/ioctl.h>
#include <fail.h>

ERRTAG = "fstrim";
ERRLIST = { 
	REPORT(EBADF), REPORT(EFAULT), REPORT(EINVAL), REPORT(EACCES),
	REPORT(EFAULT), REPORT(EINTR), REPORT(ELOOP), REPORT(EMFILE),
	REPORT(ENFILE), REPORT(ENOENT), REPORT(ENOMEM), REPORT(ENOTDIR),
	REPORT(EPERM), REPORT(EROFS), REPORT(ETXTBSY), REPORT(EWOULDBLOCK),
	RESTASNUMBERS
};

#define FITRIM _IOWR('X', 121, struct fstrim_range)

struct fstrim_range {
	uint64_t start;
	uint64_t len;
	uint64_t minlen;
};

static void parsesuffixed(uint64_t* u, const char* n)
{
	uint64_t tmp = 0;
	const char* p;
	int d;

	for(p = n; *p; p++)
		if(*p >= '0' && (d = *p - '0') <= 9)
			tmp = tmp*10 + d;
		else
			break;
	switch(*p)
	{
		default: fail("invalid number", n, 0);
		case '\0': break;
		case 'G': tmp *= 1024;
		case 'M': tmp *= 1024;
		case 'K': tmp *= 1024;
	};

	*u = tmp;
}

int main(int argc, char** argv)
{
	struct fstrim_range range = { 0, (uint64_t)-1, 0 };
	int i = 1;

	if(argc < 2)
		fail("need mountpoint to trim", NULL, 0);

	const char* mnt = argv[i++];

	if(i < argc)
		parsesuffixed(&range.minlen, argv[i++]);
	if(i < argc)
		parsesuffixed(&range.start, argv[i++]);
	if(i < argc)
		parsesuffixed(&range.len, argv[i++]);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	const long flags = O_RDONLY | O_DIRECTORY | O_NONBLOCK;
	long fd = xchk(sysopen(mnt, flags), "cannot open", mnt);

	xchk(sysioctl(fd, FITRIM, (long)&range), "ioctl failed", NULL);

	return 0;
}
