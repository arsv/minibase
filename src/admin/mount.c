#include <sys/mount.h>
#include <fail.h>

ERRTAG = "mount";
ERRLIST = {
	REPORT(EACCES), REPORT(EINVAL), REPORT(EBUSY), REPORT(EFAULT),
	REPORT(ELOOP), REPORT(EMFILE), REPORT(ENODEV), REPORT(ENOENT),
	REPORT(ENOMEM), REPORT(ENOTBLK), REPORT(ENOTDIR), REPORT(ENXIO),
	REPORT(EPERM), RESTASNUMBERS
};

/* This should not clash with any valid MS_* flags below.
   (which happen to be the case, those are (1<<0) ... (1<<25) */
#define MS_X_NONE (1<<31)

static long parseflags(const char* flagstr)
{
	const char* p;
	char flg[3] = "-?";
	long flags = 0;

	for(p = flagstr; *p; p++) switch(*p)
	{
		case 'b': flags |= MS_BIND; break;
		case 'm': flags |= MS_MOVE; break;
		case 'r': flags |= MS_RDONLY; break;
		case 't': flags |= MS_LAZYTIME; break;
		case 'd': flags |= MS_NODEV; break;
		case 'x': flags |= MS_NOEXEC; break;
		case 'u': flags |= MS_NOSUID; break;
		case 'e': flags |= MS_REMOUNT; break;
		case 's': flags |= MS_SILENT; break;
		case 'y': flags |= MS_SYNCHRONOUS; break;
		case 'v': flags |= MS_X_NONE; break;
		default: 
			flg[1] = *p;
			fail("unknown flag", flg, 0);
	}

	return flags;
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
	long flags = 0;
	char* data = NULL;

	int i = 1;

	if(i < argc && argv[i][0] == '-')
		flags = parseflags(argv[i++] + 1);

	if(i < argc)
		target = argv[i++];
	else
		fail("mountpoint required", NULL, 0);

	if(i < argc && !(flags & (MS_REMOUNT | MS_X_NONE)))
		source = argv[i++];

	flags &= ~MS_X_NONE;

	if(i < argc && !(flags & (MS_MOVE | MS_REMOUNT)))
		fstype = argv[i++];
	if(i < argc && !(flags & (MS_MOVE | MS_REMOUNT | MS_BIND)))
		data = argv[i++];
	if(i < argc)
		fail("too many arguments", NULL, 0);

	int ret = sysmount(source, target, fstype, flags, data);

	if(ret >= 0) return 0;

	if(flags & MS_REMOUNT)
		fail("cannot remount", target, -ret);
	else
		fail("cannot mount", target, -ret);

	return -1;
}
