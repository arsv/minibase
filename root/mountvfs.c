#include <bits/errno.h>
#include <bits/mount.h>
#include <sys/mount.h>

#include <strcmp.h>
#include <null.h>
#include <fail.h>

#define TAG "mountvfs"

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
} fstab[] = {
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
	int ret = sysmount("none", p->point, p->type, p->flags, p->opts);

	if(ret >= 0) return;

	fail(TAG, "cannot mount", p->point, -ret);
}

static void mountbyname(const char* name)
{
	const struct mount* p;

	for(p = fstab; p->name; p++)
		if(!strcmp(p->name, name))
			return mountentry(p);

	fail(TAG, "unknown filesystem", name, 0);
}

int main(int argc, char** argv)
{
	int i;

	if(argc < 2)
		fail(TAG, "nothing to mount", NULL, 0);
	for(i = 1; i < argc; i++)
		mountbyname(argv[i]);

	return 0;
}
