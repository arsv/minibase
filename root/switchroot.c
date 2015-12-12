#include <bits/magic.h>
#include <bits/mount.h>
#include <bits/errno.h>
#include <bits/fcntl.h>
#include <bits/stat.h>
#include <bits/statfs.h>
#include <bits/dirent.h>

#include <sys/execve.h>
#include <sys/chroot.h>
#include <sys/statfs.h>
#include <sys/dup2.h>
#include <sys/stat.h>
#include <sys/open.h>
#include <sys/chdir.h>
#include <sys/close.h>
#include <sys/getdents.h>
#include <sys/getpid.h>
#include <sys/mount.h>
#include <sys/openat.h>
#include <sys/unlinkat.h>
#include <sys/fstatat.h>

#include <fail.h>
#include <null.h>
#include <xchk.h>

/* Usage:

   	switchroot [+/dev/console] /newroot [/sbin/init ...]

   It's not clear (yet) how to handle the console part,
   but I feel it must be either dropped or hard-coded.
   There should be no reason to do tricks with /dev/console */

ERRTAG = "switchroot";
ERRLIST = {
	REPORT(EACCES), REPORT(EBADF), REPORT(EFAULT), REPORT(EINTR),
	REPORT(EIO), REPORT(ELOOP), REPORT(ENOENT), REPORT(ENOMEM),
	REPORT(ENOSYS), REPORT(ENOTDIR), REPORT(EINVAL), REPORT(ENOTDIR),
	REPORT(EBUSY), REPORT(EISDIR), REPORT(ENOMEM), REPORT(EPERM),
	REPORT(EROFS), RESTASNUMBERS
};

static int checkramfs(void)
{
	struct statfs st;

	if(sysstatfs("/", &st) < 0)
		return -1;

	if(st.f_type != RAMFS_MAGIC
	&& st.f_type != TMPFS_MAGIC)
		return -1;

	return 0;
};

#define DEBUFSIZE 2000

static inline int dotddot(const char* p)
{
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

static void deleteall(int atdir, const char* dirname, long rootdev)
{
	char debuf[DEBUFSIZE];
	struct dirent64* deptr = (struct dirent64*) debuf;
	const int delen = sizeof(debuf);
	struct stat st;
	/* open, getdents and unlink, stat return vals */
	long fd, rd, ul, sr;

	if((fd = sysopenat(atdir, dirname, O_DIRECTORY)) < 0)
		return;
	if((sr = sysfstatat(atdir, "", &st, AT_EMPTY_PATH)) < 0)
		goto out;
	if(st.st_dev != rootdev)
		goto out;

	while((rd = sysgetdents64(fd, deptr, delen)) > 0)
	{
		struct dirent64* dep = deptr;
		struct dirent64* end = deptr + rd;

		while(dep < end) {
			if(dotddot(dep->d_name))
				continue;
			if(dep->d_type == DT_DIR)
				goto isdir;

			ul = sysunlinkat(fd, dep->d_name, 0);
			if(ul >= 0 || ul != -EISDIR)
				goto next;
		isdir:
			deleteall(fd, dep->d_name, rootdev);
			sysunlinkat(fd, dep->d_name, AT_REMOVEDIR);
			/* XXX: ul=, warn if ul */
		next:		
			if(!dep->d_reclen)
				break;
			dep += dep->d_reclen;
		};
	};
out:
	sysclose(fd);
};

static void changeroot(const char* newroot)
{
	struct stat st;
	long rootdev;

	if(sysgetpid() != 1)
		fail("not running as pid 1", NULL, 0);

	xchk(syschdir(newroot), "chdir", newroot);
	xchk(sysstat("/", &st), "stat", "/");
	rootdev = st.st_dev;

	xchk(sysstat(".", &st), "stat", newroot);

	if(st.st_dev == rootdev)
		fail("new root is on the same fs", NULL, 0);
	if(sysstat("/init", &st) < 0 || !S_ISREG(st.st_mode))
		fail("/init is not a regular file", NULL, 0);

	if(checkramfs())
		fail("not running on an initramfs", NULL, 0);

	deleteall(AT_FDCWD, "/", rootdev);

	xchk(sysmount(".", "/", NULL, MS_MOVE, NULL), "mount", ". to /");
	xchk(syschroot("."), "chroot", ".");
	xchk(syschdir("/"), "chdir", "/");
}

static void changefds(const char* console)
{
	long fd = sysopen(console, O_RDWR);
	if(fd < 0) return;
	sysclose(0);
	sysdup2(0, 1);
	sysdup2(0, 2);
}

int main(int argc, char** argv)
{
	char* console = NULL;

	int i = 1;

	if(i < argc && argv[i][0] == '+')
		console = argv[i++] + 1;
	if(console && !*console)
		console = "/dev/console";

	if(i >= argc)
		fail("no newroot to switch to", NULL, 0);

	changeroot(argv[i++]);

	if(console)
		changefds(console);

	if(i <= argc) {
		argv += i;
	} else {
		/* no init has been supplied */
		argv[0] = "/sbin/init";
		argv[1] = NULL;
	}

	long ret = sysexecve(*argv, argv, NULL);
	fail("cannot exec", *argv, -ret);
}
