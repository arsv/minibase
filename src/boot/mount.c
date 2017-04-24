#include <sys/mount.h>
#include <sys/umount.h>
#include <fail.h>

ERRTAG = "mount";
ERRLIST = {
	REPORT(EACCES), REPORT(EINVAL), REPORT(EBUSY), REPORT(EFAULT),
	REPORT(ELOOP), REPORT(EMFILE), REPORT(ENODEV), REPORT(ENOENT),
	REPORT(ENOMEM), REPORT(ENOTBLK), REPORT(ENOTDIR), REPORT(ENXIO),
	REPORT(EPERM), RESTASNUMBERS
};

#define OPT_u (1<<0)	/* umount */
#define OPT_v (1<<1)	/* mount none, virtual fs */

struct flag {
	char key;
	int val;
} mountflags[] = {
	{ 'b', MS_BIND },
	{ 'm', MS_MOVE },
	{ 'r', MS_RDONLY },
	{ 't', MS_LAZYTIME },
	{ 'd', MS_NODEV },
	{ 'x', MS_NOEXEC },
	{ 's', MS_NOSUID },
	{ 'e', MS_REMOUNT },
	{ 'i', MS_SILENT },
	{ 'y', MS_SYNCHRONOUS },
	{ 0, 0 }
}, umountflags[] = {
	{ 'f', MNT_FORCE },
	{ 'd', MNT_DETACH },
	{ 'x', MNT_EXPIRE },
	{ 'n', UMOUNT_NOFOLLOW },
	{ 0, 0 }
};

static void badflag(char f)
{
	char flg[3];

	flg[0] = '-';
	flg[1] = f;
	flg[2] = '\0';

	fail("unknown flag", flg, 0);
}

static void baduvuse(void)
{
	fail("-u and -v may only be used in the first position", NULL, 0);
}

static int parseflags(const char* str, long* out)
{
	const char* p;
	struct flag* f;
	long flags = 0;
	int opts = 0;

	struct flag* table = mountflags;

	if(*str == 'u') {
		table = umountflags;
		opts |= OPT_u;
		str++;
	} else if(*str == 'v') {
		opts |= OPT_v;
		str++;
	};

	for(p = str; *p; p++) {
		for(f = table; f->key; f++)
			if(f->key == *p) {
				flags |= f->val;
				break;
			};
		if(f->key)
			continue;
		else if(*p == 'u' || *p == 'v')
			baduvuse();
		else
			badflag(*p);
	}

	*out = flags;
	return opts;
}

static int umount(int i, int argc, char** argv, long flags)
{
	long ret;

	for(; i < argc; i++)
		if((ret = sysumount(argv[i], flags)) < 0)
			fail("cannot umount", argv[i], ret);

	return 0;
}

/* sysmount may ignore source/fstype/data depending on the flags set.
   The tool accounts for this by not expecting respective arguments
   in the command line, so that it's

   	mount -r /mnt/blah /dev/foo ext4 discard

   in the full case but not when remounting:

   	mount -m /target options

   Otherwise, we follow the syscall pretty closely.
   No writes to mtab of course, there's /proc/mounts for that. */

int main(int argc, char** argv)
{
	char* source = NULL;
	char* target = NULL;
	char* fstype = NULL;
	char* data = NULL;
	long flags = 0;
	int opts = 0;

	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = parseflags(argv[i++] + 1, &flags);

	if(opts & OPT_u)
		return umount(i, argc, argv, flags);

	if(i < argc)
		target = argv[i++];
	else
		fail("mountpoint required", NULL, 0);

	if((i < argc) && !(flags & MS_REMOUNT) && !(opts & OPT_v))
		source = argv[i++];

	if(i < argc && !(flags & (MS_MOVE | MS_REMOUNT)))
		fstype = argv[i++];
	if(i < argc && !(flags & (MS_MOVE | MS_REMOUNT | MS_BIND)))
		data = argv[i++];
	if(i < argc)
		fail("too many arguments", NULL, 0);

	int ret = sysmount(source, target, fstype, flags, data);

	if(ret >= 0) return 0;

	if(flags & MS_REMOUNT)
		fail("cannot remount", target, ret);
	else
		fail("cannot mount", target, ret);

	return -1;
}
