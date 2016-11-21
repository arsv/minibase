#include <sys/open.h>
#include <sys/close.h>
#include <sys/utimensat.h>

#include <util.h>
#include <fail.h>

/* Ten times out of ten touch is used to (safely) create an empty file.
   In exceptional cases, I think it might be used to update timestamps
   to current time (i.e. "touch" a file).

   Updates to given time, or to a reference file, sound like something
   that just does not happen in the wild. The only reason to implement
   them is to have *some* way to alter file's atime/mtime, and that
   should probably be in a different tool anyway. */

#define OPTS "am"
#define OPT_a (1<<0)
#define OPT_m (1<<1)

ERRTAG = "touch";
ERRLIST = {
	REPORT(EACCES), REPORT(ENOENT), REPORT(EPERM), REPORT(EINVAL),
	REPORT(EDQUOT), REPORT(EFAULT), REPORT(EINTR), REPORT(EISDIR),
	REPORT(ELOOP), REPORT(EMFILE), REPORT(ENFILE), REPORT(ENODEV),
	REPORT(ENOMEM), REPORT(ENOSPC), REPORT(ENOTDIR), REPORT(ENXIO),
	REPORT(EOVERFLOW), REPORT(EPERM), REPORT(EROFS), REPORT(ETXTBSY),
	RESTASNUMBERS
};

static void touch(char* path, struct timespec* times)
{
	long ret = sysutimensat(AT_FDCWD, path, times, AT_SYMLINK_NOFOLLOW);

	if(ret >= 0)
		return;
	if(ret != -ENOENT)
		fail("cannot update times for", path, ret);

	/* The file does not exist, let's try to creat it */

	const int flags = O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW;
	long fd = sysopen3(path, flags, 0666);

	if(fd < 0)
		fail("cannot create", path, fd);
	else
		sysclose(fd);
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;
	struct timespec times[2];
	struct timespec* pt = NULL;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(i >= argc)
		fail("missing file operand", NULL, 0);

	if(opts & (OPT_a | OPT_m)) {
		pt = times;
		times[0].tv_nsec = (opts & OPT_a) ? UTIME_NOW : UTIME_OMIT;
		times[1].tv_nsec = (opts & OPT_m) ? UTIME_NOW : UTIME_OMIT;
	}

	for(; i < argc; i++)
		touch(argv[i], pt);

	return 0;
}
