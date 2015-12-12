#include <bits/errno.h>
#include <bits/mount.h>
#include <sys/umount.h>

#include <fail.h>
#include <null.h>

ERRTAG = "umount";
ERRLIST = {
	REPORT(EAGAIN), REPORT(EBUSY), REPORT(EFAULT), REPORT(EINVAL),
	REPORT(ENOENT), REPORT(ENOMEM), REPORT(EPERM), RESTASNUMBERS
};

static long parseflags(const char* flagstr)
{
	const char* p;
	char flg[3] = "-?";
	long flags = 0;

	for(p = flagstr; *p; p++) switch(*p)
	{
		case 'f': flags |= MNT_FORCE; break;
		case 'd': flags |= MNT_DETACH; break;
		case 'x': flags |= MNT_EXPIRE; break;
		case 'n': flags |= UMOUNT_NOFOLLOW; break;
		default: 
			flg[1] = *p;
			fail("unknown flag", flg, 0);
	}

	return flags;
}

int main(int argc, char** argv)
{
	int i = 1;
	int flags = 0;
	long ret;

	if(i < argc && argv[i][0] == '-')
		flags = parseflags(argv[i++] + 1);

	if(i >= argc)
		fail("nothing to umount", NULL, 0);

	for(; i < argc; i++)
		if((ret = sysumount(argv[i], flags)) < 0)
			fail("cannot umount", argv[i], -ret);

	return 0;
}
