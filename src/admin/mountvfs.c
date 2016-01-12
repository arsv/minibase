#include <sys/mount.h>
#include <sys/umount.h>
#include <sys/mkdir.h>
#include <sys/statfs.h>

#include <argbits.h>
#include <strcmp.h>
#include <strlen.h>
#include <memcpy.h>
#include <fail.h>

ERRTAG = "mountvfs";
ERRLIST = {
	REPORT(EACCES), REPORT(EINVAL), REPORT(EBUSY), REPORT(EFAULT),
	REPORT(ELOOP), REPORT(EMFILE), REPORT(ENODEV), REPORT(ENOENT),
	REPORT(ENOMEM), REPORT(ENOTBLK), REPORT(ENOTDIR), REPORT(ENXIO),
	REPORT(EPERM), RESTASNUMBERS
};

#define SX	(MS_NOSUID | MS_NOEXEC)
#define SD	(MS_NOSUID | MS_NODEV)
#define SDX	(MS_NOSUID | MS_NODEV | MS_NOEXEC)

static const struct mount {
	char* name;
	char* type;
	char* point;
	long flags;
	char* opts;
} vfstab[] = {
 { "dev",	"devtmpfs",	"/dev",				SX,	NULL },
 { "pts",	"devpts",	"/dev/pts",			SX,	NULL },
 { "shm",	"tmpfs",	"/dev/shm",			SX,	NULL },
 { "mq",	"mqueue",	"/dev/mqueue",			0,	NULL },
 { "proc",	"proc",		"/proc",			SDX,	NULL },
 { "sys",	"sysfs",	"/sys",				SDX,	NULL },
 { "run",	"tmpfs",	"/run",				SD,	NULL },
 { "tmp",	"tmpfs",	"/tmp",				SD,	NULL },
 { "config",	"configfs",	"/sys/kernel/config",		0,	NULL },
 { "debug",	"debugfs",	"/sys/kernel/debug",		0,	NULL },
 { "trace",	"tracefs",	"/sys/kernel/debug/tracing",	0,	NULL },
};

static const int nvfs = sizeof(vfstab) / sizeof(*vfstab);

static const struct mount* findentry(const char* name)
{
	const struct mount* p;

	for(p = vfstab; p < vfstab + nvfs; p++)
		if(!strcmp(p->name, name))
			return p;

	fail("unknown filesystem", name, 0);
}

/* Mounting. There is no default run (w/o arguments),
   the tags must always be supplied.

   For some cases we need, and for most we can just in case
   try to make the mountpoint if does not exist.

   For totally vain reasons, namely to avoid one redundant call
   in the better case, we attempt to mountentry() and only if
   that fails do mkdir-mountentry sequence. The alternative is
   doing mkdir first, expecting EEXIST. */

static long mountentry(const struct mount* p)
{
	return sysmount("none", p->point, p->type, p->flags, p->opts);
}

static void chkmount(const struct mount* p)
{
	long ret;

	if((ret = mountentry(p)) >= 0)
		return;
	if(ret != -ENOENT)
		goto err;

	if((ret = sysmkdir(p->point, 0755)) < 0)
		goto err;
	if((ret = mountentry(p)) >= 0)
		return;

err:	fail("cannot mount", p->point, -ret);
}

static void mounttags(int i, int argc, char** argv)
{
	if(i >= argc)
		fail("nothing to mount", NULL, 0);
	for(; i < argc; i++)
		chkmount(findentry(argv[i]));
}

/* Moving. No tags means "move all mounted VFSes".

   We tell mounted from not-mounted by comparing statfs.f_fsid.
   The test is not exactly reliable, and there are nested entries,
   so error reporting is somewhat relaxed.

   For explicitly specified tags, any errors are reported.
   The assumption is that if the user listed a dir, it must be there. */

static long movemount(char* target, char* source)
{
	long slen = strlen(source);
	long tlen = strlen(target);
	char fulltarget[slen + tlen + 2];

	memcpy(fulltarget, target, tlen);
	memcpy(fulltarget + tlen, source, slen);
	fulltarget[tlen+slen] = '\0';

	return sysmount(source, fulltarget, NULL, MS_MOVE, NULL);
}

static void chkmove(char* target, const struct mount* p)
{
	xchk(movemount(target, p->point), "cannot move", p->point);
}

static void trymove(char* target, const struct mount* p, struct statfs* rst)
{
	struct statfs pst;
	long ret;

	if((ret = sysstatfs(p->point, &pst)) < 0)
		return;
	if(pst.f_fsid == rst->f_fsid)
		return;

	ret = movemount(target, p->point);

	if(ret > 0 || ret == -EINVAL)
		return;

	warn("cannot move", p->point, -ret);
}

static void movetags(int i, int argc, char** argv)
{
	if(i >= argc)
		fail("target directory expected", NULL, 0);

	char* target = argv[i++];

	if(i < argc) {
		while(i < argc)
			chkmove(target, findentry(argv[i++]));
	} else {
		long ret;
		const struct mount* p;
		struct statfs st;

		if((ret = sysstatfs("/", &st)) < 0)
			fail("cannot statfs", "/", -ret);

		for(p = vfstab; p < vfstab + nvfs; p++)
			trymove(target, p, &st);
	}
}

/* Unmounting. For no-arg case, this is done in reverse order,
   so that /dev/pts is umounted before /dev

   We do not attempt to check whether the point is mounted,
   that's at least one syscall per mountpoint and we can just
   as well by simply calling sysumount(). */

static long umountentry(const struct mount* p)
{
	return sysumount(p->point, MNT_DETACH);
}

static void chkumount(const struct mount* p)
{
	xchk(umountentry(p), "cannot umount", p->point);
}

static void umounttags(int i, int argc, char** argv)
{
	const struct mount* p;

	if(i < argc)
		while(i < argc)
			chkumount(findentry(argv[i++]));
	else
		for(p = vfstab + nvfs - 1; p >= vfstab; p--)
			umountentry(p);
}

#define OPTS "mu"
#define OPT_m (1<<0)
#define OPT_u (1<<1)

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if((opts & OPT_m) && (opts & OPT_u))
		fail("cannot use -u and -m at the same time", NULL, 0);

	if(opts & OPT_u)
		umounttags(i, argc, argv);
	else if(opts & OPT_m)
		movetags(i, argc, argv);
	else
		mounttags(i, argc, argv);

	return 0;
}
