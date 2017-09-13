#include <sys/mount.h>

#include <errtag.h>
#include <string.h>
#include <util.h>

ERRTAG("mount");
ERRLIST(NEACCES NEINVAL NEBUSY NEFAULT NELOOP NEMFILE NENODEV NENOENT
	NENOMEM NENOTBLK NENOTDIR NENXIO NEPERM NEROFS);

/* Pre-defined virtual filesystems. Most of the time these are mounted
   at exactly the same locations, with exactly the same options, so no
   point in forcing the users to script it all the time. */

#define SX	(MS_NOSUID | MS_NOEXEC)
#define SD	(MS_NOSUID | MS_NODEV)
#define SDX	(MS_NOSUID | MS_NODEV | MS_NOEXEC)

static const struct vfs {
	char* name;
	char* type;
	long flags;
	char* point;
} vfstab[] = {
	{ "dev",     "devtmpfs",   SX,   "/dev",                      },
	{ "pts",     "devpts",     SX,   "/dev/pts",                  },
	{ "shm",     "tmpfs",      SX,   "/dev/shm",                  },
	{ "mq",      "mqueue",     0,    "/dev/mqueue",               },
	{ "proc",    "proc",       SDX,  "/proc",                     },
	{ "sys",     "sysfs",      SDX,  "/sys",                      },
	{ "run",     "tmpfs",      SD,   "/run",                      },
	{ "tmp",     "tmpfs",      SD,   "/tmp",                      },
	{ "mnt",     "tmpfs",      SD,   "/mnt",                      },
	{ "config",  "configfs",   0,    "/sys/kernel/config",        },
	{ "debug",   "debugfs",    0,    "/sys/kernel/debug",         },
	{ "trace",   "tracefs",    0,    "/sys/kernel/debug/tracing", },
	{ NULL,      NULL,         0,    NULL                         }
};

static int mountcg(char* path)
{
	char* pref = "/sys/fs/cgroup/";
	int len = strlen(pref);

	if(strncmp(path, pref, len))
		return 0;
	if(!path[len])
		return 0;

	char* group = path + len;
	int flags = MS_NODEV | MS_NOSUID | MS_NOEXEC | MS_RELATIME;

	xchk(sys_mount(NULL, path, "cgroup", flags, group), path, NULL);

	return 1;
}

static void mountvfs(char* tag, long flags)
{
	const struct vfs* p;

	if(mountcg(tag))
		return;

	for(p = vfstab; p->name; p++)
		if(!strcmp(p->name, tag))
			break;
		else if(!strcmp(p->point, tag))
			break;
	if(!p->name)
		fail("unknown vfs", tag, 0);

	flags |= p->flags | MS_RELATIME;

	xchk(sys_mount(NULL, p->point, p->type, flags, ""), p->point, NULL);
}

static void mntvfs(int argc, char** argv, int i, char* flagstr)
{
	if(i >= argc)
		fail("nothing to mount", NULL, 0);
	for(; i < argc; i++)
		mountvfs(argv[i], 0);
}

/* mount(2) may ignore source/fstype/data depending on the flags set.
   The tool accounts for this by not expecting respective arguments
   in the command line, so that it's

   	mount -r /mnt/blah /dev/foo ext4 discard

   in the full case but not when remounting:

   	mount -m /target options

   Otherwise, we follow the syscall pretty closely.
   No writes to mtab of course, there's /proc/mounts for that. */

#define MOUNT_NONE (1<<30)
#define MOUNT_MASK (~MOUNT_NONE)

struct flag {
	char key;
	int val;
} mountflags[] = {
	{ 'b', MS_BIND },
	{ 'm', MS_MOVE },
	{ 'r', MS_RDONLY },
	{ 'l', MS_LAZYTIME },
	{ 'd', MS_NODEV },
	{ 'x', MS_NOEXEC },
	{ 's', MS_NOSUID },
	{ 'e', MS_REMOUNT },
	{ 'i', MS_SILENT },
	{ 'y', MS_SYNCHRONOUS },
	{ 'n', MOUNT_NONE },
	{ 0, 0 }
}, umountflags[] = {
	{ 'f', MNT_FORCE },
	{ 'd', MNT_DETACH },
	{ 'x', MNT_EXPIRE },
	{ 'n', UMOUNT_NOFOLLOW },
	{ 0, 0 }
};

static long parse_flags(struct flag* table, char* str)
{
	char* p;
	struct flag* f;
	long flags = 0;
	char flg[3] = "-\0";

	for(p = str; *p; p++) {
		for(f = table; f->key; f++)
			if(f->key == *p)
				break;
		if(!f->key)
			goto bad;

		flags |= f->val;
	}

	return flags;
bad:
	flg[1] = *p;
	fail("unknown flag", flg, 0);
}

static int umount(int argc, char** argv, int i, char* flagstr)
{
	long flags = parse_flags(umountflags, flagstr);

	for(; i < argc; i++)
		xchk(sys_umount(argv[i], flags), argv[i], NULL);

	return 0;
}

static void mount(int argc, char** argv, int i, char* flagstr)
{
	long flags = parse_flags(mountflags, flagstr);
	char *source, *target, *fstype, *data;

	if(i < argc)
		target = argv[i++];
	else
		fail("mountpoint required", NULL, 0);

	if(flags & (MS_REMOUNT | MOUNT_NONE))
		source = NULL;
	else if(i < argc)
		source = argv[i++];
	else if(i == argc)
		return mountvfs(target, flags);
	else
		fail("need device to mount", NULL, 0);

	if(flags & (MS_MOVE | MS_REMOUNT))
		fstype = NULL;
	else if(i < argc)
		fstype = argv[i++];
	else
		fail("missing fstype", NULL, 0);

	if(i < argc && !(flags & (MS_MOVE | MS_REMOUNT | MS_BIND)))
		data = argv[i++];
	else
		data = NULL;

	if(i < argc)
		fail("too many arguments", NULL, 0);

	flags &= MOUNT_MASK;

	/* Errors from mount(2) may apply to either the source *or* the target.
	   There's no way to tell, so generic "mount: errno" message got to be
	   the least confusing one. */
	xchk(sys_mount(source, target, fstype, flags, data), NULL, NULL);
}


int main(int argc, char** argv)
{
	if(argc < 2)
		fail("too few arguments", NULL, 0);

	if(argv[1][0] != '-')
		mount(argc, argv, 1, "");
	else if(argv[1][1] == 'u')
		umount(argc, argv, 2, argv[1] + 2);
	else if(argv[1][1] == 'v')
		mntvfs(argc, argv, 2, argv[1] + 2);
	else
		mount(argc, argv, 2, argv[1] + 1);

	return 0;
}
