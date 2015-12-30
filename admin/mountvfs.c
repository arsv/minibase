#include <bits/errno.h>
#include <bits/mount.h>
#include <bits/statfs.h>
#include <sys/mount.h>
#include <sys/statfs.h>

#include <argbits.h>
#include <strcmp.h>
#include <strlen.h>
#include <memcpy.h>
#include <null.h>
#include <fail.h>
#include <xchk.h>

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
 { NULL }
};

static void mountentry(const struct mount* p)
{
	xchk( sysmount("none", p->point, p->type, p->flags, p->opts),
		"cannot mount", p->point);
}

static const struct mount* findentry(const char* name)
{
	const struct mount* p;

	for(p = vfstab; p->name; p++)
		if(!strcmp(p->name, name))
			return p;

	fail("unknown filesystem", name, 0);
}

static void mounttags(int i, int argc, char** argv)
{
	if(i >= argc)
		fail("nothing to mount", NULL, 0);
	for(; i < argc; i++)
		mountentry(findentry(argv[i]));
}

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

		for(p = vfstab; p->name; p++)
			trymove(target, p, &st);
	}
}

#define OPTS "m"
#define OPT_m (1<<0)

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts & OPT_m)
		movetags(i, argc, argv);
	else
		mounttags(i, argc, argv);

	return 0;
}
