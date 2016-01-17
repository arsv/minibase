#include <sys/open.h>
#include <sys/sync.h>		/* Who could have thought it's a good idea */
#include <sys/syncfs.h>		/* to have these *four* syscalls instead   */
#include <sys/fsync.h>		/* of one taking optional fd and flags?    */
#include <sys/fdatasync.h>

#include <argbits.h>
#include <fail.h>

#define OPTS "fd"
#define OPT_f (1<<0)
#define OPT_d (1<<1)

ERRTAG = "sync";
ERRLIST = {
	REPORT(EBADF), REPORT(EIO), REPORT(EROFS), REPORT(EINVAL),
	REPORT(EACCES), REPORT(ENOENT), REPORT(EFAULT), REPORT(EFBIG),
	REPORT(EINTR), REPORT(ELOOP), REPORT(ENFILE), REPORT(EMFILE),
	REPORT(ENODEV), REPORT(ENOMEM), REPORT(ENOTDIR), REPORT(EPERM),
	REPORT(EWOULDBLOCK), RESTASNUMBERS
};

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if((opts & (OPT_f | OPT_d)) == (OPT_f | OPT_d))
		fail("cannot use -f and -d at once", NULL, 0);
	if((opts & (OPT_f | OPT_d)) && i >= argc)
		fail("file names must be specified with -f and -d", NULL, 0);

	if(i >= argc) {
		xchk(syssync(), "sync", NULL);
	} else for(; i < argc; i++) {
		const int flags = O_RDONLY | O_NONBLOCK;
		const char* name = argv[i];

		long fd = xchk(sysopen(name, flags), "open", name);

		if(opts & OPT_d)
			xchk(sysfdatasync(fd), "fdatasync", name);
		else if(opts & OPT_f)
			xchk(syssyncfs(fd), "syncfs", name);
		else
			xchk(sysfsync(fd), "fsync", name);
	}

	return 0;
}
