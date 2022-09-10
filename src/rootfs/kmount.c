#include <sys/mount.h>
#include <sys/fpath.h>

#include <string.h>
#include <util.h>
#include <main.h>

ERRTAG("kmount");
ERRLIST(NEACCES NEINVAL NEBUSY NEFAULT NELOOP NEMFILE NENODEV NENOENT
	NENOMEM NENOTBLK NENOTDIR NENXIO NEPERM NEROFS);

/* Pre-defined virtual filesystems. Most of the time these are mounted
   at exactly the same locations, with exactly the same options, so no
   point in forcing the users to script it. */

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

#define OPTS "bmrldxseiynfuvc"
#define OPT_b (1<<0)
#define OPT_m (1<<1)
#define OPT_r (1<<2)
#define OPT_l (1<<3)
#define OPT_d (1<<4)
#define OPT_x (1<<5)
#define OPT_s (1<<6)
#define OPT_e (1<<7)
#define OPT_i (1<<8)
#define OPT_y (1<<9)
#define OPT_n (1<<10)
#define OPT_f (1<<11)
#define OPT_u (1<<12)
#define OPT_v (1<<13)
#define OPT_c (1<<14)

struct flag {
	int opt;
	int val;
} mountflags[] = {
	{ OPT_b, MS_BIND },
	{ OPT_m, MS_MOVE },
	{ OPT_r, MS_RDONLY },
	{ OPT_l, MS_LAZYTIME },
	{ OPT_d, MS_NODEV },
	{ OPT_x, MS_NOEXEC },
	{ OPT_s, MS_NOSUID },
	{ OPT_e, MS_REMOUNT },
	{ OPT_i, MS_SILENT },
	{ OPT_y, MS_SYNCHRONOUS },
	{ 0, 0 }
}, umountflags[] = {
	{ OPT_f, MNT_FORCE },
	{ OPT_d, MNT_DETACH },
	{ OPT_x, MNT_EXPIRE },
	{ OPT_n, UMOUNT_NOFOLLOW },
	{ 0, 0 }
};

static int opts2flags(struct flag* table, int opts)
{
	struct flag* f;
	int flags = 0;

	for(f = table; f->opt; f++) {
		if(opts & f->opt) {
			opts &= ~(f->opt);
			flags |= f->val;
		}
	}

	if(opts)
		fail("extra options", NULL, 0);

	return flags;
}

static void create_mountpoint(char* path)
{
	int ret;

	if((ret = sys_mkdir(path, 0000)) >= 0)
		return;
	if(ret == -EEXIST)
		return;

	fail("mkdir", path, ret);
}

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
	int ret;

	if((ret = sys_mount(NULL, path, "cgroup", flags, group)) < 0)
		fail(NULL, path, ret);

	return 1;
}

static void virtual(char* tag, int opts)
{
	const struct vfs* p;
	int ret;

	if(mountcg(tag))
		return;

	for(p = vfstab; p->name; p++)
		if(!strcmp(p->name, tag))
			break;
		else if(!strcmp(p->point, tag))
			break;
	if(!p->name)
		fail("unknown vfs", tag, 0);

	int flags = p->flags | MS_RELATIME;

	if(opts & OPT_c)
		create_mountpoint(p->point);

	if((ret = sys_mount(NULL, p->point, p->type, flags, "")) < 0)
		fail("mount", p->point, ret);
}

static void mntvfs(int argc, char** argv, int i, int opts)
{
	if(i >= argc)
		fail("nothing to mount", NULL, 0);
	if(opts & ~OPT_c)
		fail("extra options", NULL, 0);

	for(; i < argc; i++)
		virtual(argv[i], opts);
}

static void umount(int argc, char** argv, int i, int opts)
{
	long flags = opts2flags(umountflags, opts & ~OPT_c);
	int ret, code = 0;

	for(; i < argc; i++) {
		if((ret = sys_umount(argv[i], flags)) < 0) {
			warn("umount", argv[i], ret);
			code = 0xFF;
		} else if(!(opts & OPT_c)) {
			;
		} else if((ret = sys_rmdir(argv[i])) < 0) {
			warn("rmdir", argv[i], ret);
		}
	}

	if(code) _exit(code);
}

/* mount(2) may ignore source/fstype/data depending on the flags set.
   The tool accounts for this by not expecting respective arguments
   in the command line, so that it's

	mount -r /mnt/blah /dev/foo ext4 discard

   in the full case but not when remounting:

	mount -m /target options

   Otherwise, we follow the syscall pretty closely.
   No writes to mtab of course, there's /proc/mounts for that. */

static void mount(int argc, char** argv, int i, int opts)
{
	int flags = opts2flags(mountflags, opts & ~OPT_m);
	char *source, *target, *fstype, *data;
	int ret;

	if(i < argc)
		target = argv[i++];
	else
		fail("mountpoint required", NULL, 0);

	if((flags & MS_REMOUNT) || (opts & OPT_n))
		source = NULL;
	else if(i < argc)
		source = argv[i++];
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

	if(opts & OPT_m)
		create_mountpoint(target);

	/* Errors from mount(2) may apply to either the source *or* the target.
	   There's no way to tell, so generic "mount: errno" message got to be
	   the least confusing one. */

	if((ret = sys_mount(source, target, fstype, flags, data)) < 0)
		fail(NULL, NULL, ret);
}

int main(int argc, char** argv)
{
	int i = 1, opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts & OPT_u)
		umount(argc, argv, i, opts & ~OPT_u);
	else if(opts & OPT_v)
		mntvfs(argc, argv, i, opts & ~OPT_v);
	else
		mount(argc, argv, i, opts);

	return 0;
}
