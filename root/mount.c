#include <bits/errno.h>
#include <bits/mount.h>
#include <sys/mount.h>

#include <fail.h>
#include <null.h>

ERRTAG = "mount";
ERRLIST = {
	REPORT(EACCES), REPORT(EINVAL), REPORT(EBUSY), REPORT(EFAULT),
	REPORT(ELOOP), REPORT(EMFILE), REPORT(ENODEV), REPORT(ENOENT),
	REPORT(ENOMEM), REPORT(ENOTBLK), REPORT(ENOTDIR), REPORT(ENXIO),
	REPORT(EPERM), RESTASNUMBERS
};

static long parseflags(const char* flagstr)
{
	const char* p;
	char flg[3] = "-?";
	long flags = 0;

	for(p = flagstr; *p; p++) switch(*p)
	{
		case 'b': flags |= MS_BIND; break;
		case 'v': flags |= MS_MOVE; break;
		case 'r': flags |= MS_RDONLY; break;
		case 't': flags |= MS_LAZYTIME; break;
		case 'd': flags |= MS_NODEV; break;
		case 'x': flags |= MS_NOEXEC; break;
		case 'u': flags |= MS_NOSUID; break;
		case 'm': flags |= MS_REMOUNT; break;
		case 's': flags |= MS_SILENT; break;
		case 'y': flags |= MS_SYNCHRONOUS; break;
		default: 
			flg[1] = *p;
			fail("unknown flag", flg, 0);
	}

	return flags;
}

/* sysmount may ignore source/fstype/data depending on the flags set.
   The tool accounts for this by not expecting respective arguments
   in the command line, so that it's

   	mount -r /dev/foo /mnt/blah ext4 discard

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

	if(argc <= 1)
		fail("too few arguments", NULL, 0);

	if(argv[i][0] == '-')
		flags = parseflags(argv[i++] + 1);

	if(i < argc && !(flags & MS_REMOUNT))
		source = argv[i++];
	if(i < argc)
		target = argv[i++];
	else
		fail("too few arguments", NULL, 0);

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
