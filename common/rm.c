#include <bits/errno.h>
#include <bits/stat.h>
#include <bits/fcntl.h>
#include <bits/dirent.h>
#include <sys/unlinkat.h>
#include <sys/fstat.h>
#include <sys/open.h>
#include <sys/close.h>
#include <sys/getdents.h>
#include <sys/fstatat.h>
#include <sys/unlink.h>
#include <sys/rmdir.h>
#include <sys/lstat.h>
#include <sys/_exit.h>

#include <argbits.h>
#include <strlen.h>
#include <fail.h>
#include <fmtstr.h>
#include <fmtchar.h>
#include <null.h>
#include <xchk.h>

#define OPTS "rfxdZ"
#define OPT_r (1<<0)
#define OPT_f (1<<1)
#define OPT_x (1<<2)
#define OPT_d (1<<3)
#define OPT_Z (1<<4)

#define DEBUFSIZE 2000

ERRTAG = "rm";
ERRLIST = {
	REPORT(EACCES), REPORT(EBUSY), REPORT(EFAULT), REPORT(EIO),
	REPORT(EISDIR), REPORT(ELOOP), REPORT(ENOENT), REPORT(ENOMEM),
	REPORT(ENOTDIR), REPORT(EPERM), REPORT(EROFS), REPORT(EOVERFLOW),
	REPORT(ENOSYS), REPORT(EBADF), REPORT(EINVAL), RESTASNUMBERS
};

static void mfail(int opts, const char* msg, const char* obj, int err)
{
	warn(msg, obj, err);
	if(!(opts & OPT_f)) _exit(-1);
}

static inline int dotddot(const char* p)
{
	if(!p[0])
		return 1;
	if(p[0] == '.' && !p[1])
		return 1;
	if(p[1] == '.' && !p[2])
		return 1;
	return 0;
}

static int samefs(const char* dirname, int dirfd, long rootdev)
{
	struct stat st;

	long ret = sysfstat(dirfd, &st);

	if(ret < 0)
		warn("cannot stat", dirname, -ret);

	/* err on the safe side */
	return (ret >= 0 && st.st_dev == rootdev);
}

static void removeany(const char* name, int type, long rootdev, int opts);
static void removedep(const char* dirname, struct dirent64* dep,
		long rootdev, int opts);

static void removedir(const char* dirname, long rootdev, int opts)
{
	char debuf[DEBUFSIZE];
	const int delen = sizeof(debuf);
	long dirfd, rd;

	if((dirfd = sysopen(dirname, O_DIRECTORY)) < 0)
		return mfail(opts, "cannot open", dirname, -dirfd);

	if((opts & OPT_x) && !samefs(dirname, dirfd, rootdev))
		goto out;

	while((rd = sysgetdents64(dirfd, (struct dirent64*)debuf, delen)) > 0)
	{
		char* ptr = debuf;
		char* end = debuf + rd;

		while(ptr < end)
		{
			struct dirent64* dep = (struct dirent64*) ptr;

			if(!dotddot(dep->d_name))
				removedep(dirname, dep, rootdev, opts);
			if(!dep->d_reclen)
				break;

			ptr += dep->d_reclen;
		}
	};
out:
	sysclose(dirfd);
};

static void removedep(const char* dirname, struct dirent64* dep,
		long rootdev, int opts)
{
	int dirnlen = strlen(dirname);
	int depnlen = strlen(dep->d_name);
	char fullname[dirnlen + depnlen + 2];

	char* p = fullname;
	char* e = fullname + sizeof(fullname) - 1;

	p = fmtstr(p, e, dirname);
	p = fmtchar(p, e, '/');
	p = fmtstr(p, e, dep->d_name);
	*p++ = '\0';

	removeany(fullname, dep->d_type, rootdev, opts);
}

/* This gets called for any node within the tree, including directories.
   To unlink the node properly, we need to know whether it's a file
   or a dir. Now instead of stat()ing it to tell that, we go on and try
   to unlink() it as a regular file. If that fails with EISDIR, we know
   it's a directory.

   In case getdents64() was kind enough to tell us node type, and it
   happens to be a directory, the check is skipped. */

static void removeany(const char* name, int type, long rootdev, int opts)
{
	long ret;

	if(type == DT_DIR)
		goto dir;

	ret = sysunlink(name);

	if(ret >= 0)
		return;
	if(!(opts & OPT_r) || ret != -EISDIR)
		goto err;
dir:
	removedir(name, rootdev, opts);

	ret = sysrmdir(name);

	if(ret >= 0)
		return;
err:
	mfail(opts, "cannot unlink", name, -ret);
}

static int samefile(struct stat* a, struct stat* b)
{
	return ((a->st_dev == b->st_dev) && (a->st_ino == b->st_ino));
}

/* This function handles top-level rm arguments only.
   When runnig without -x option, top-level arguments are stat'ed
   for st_dev value, which is then used to limit cross-device recursion. */

static void remove(const char* name, int opts, struct stat* rst)
{
	long ret = 0;
	long dev = 0;

	if(opts & OPT_r) {
		/* make extra sure we're doing the right thing with -r */
		struct stat st;

		if((ret = syslstat(name, &st)) < 0)
			return mfail(opts, "cannot stat", name, -ret);

		if(!(opts & OPT_Z) && samefile(&st, rst))
			return mfail(opts, "refusing to delete root", NULL, 0);

		if(!(opts & OPT_x))
			dev = st.st_dev;
	}

	/* if we are called with -d, force directory handling */
	const int type = (opts & OPT_d ? DT_DIR : DT_UNKNOWN);

	removeany(name, type, dev, opts);
}

int main(int argc, char** argv)
{
	int opts = 0, i = 1;
	struct stat st;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if((opts & (OPT_r | OPT_Z)) == OPT_r)
		xchk(syslstat("/", &st), "cannot stat", "/");

	if(i >= argc)
		fail("need file names to remove", NULL, 0);

	while(i < argc)
		remove(argv[i++], opts, &st);

	return 0;
}
