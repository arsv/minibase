#include <sys/open.h>
#include <sys/sync.h>
#include <sys/read.h>
#include <sys/mount.h>
#include <sys/umount.h>
#include <sys/reboot.h>

#include <string.h>
#include <util.h>
#include <heap.h>
#include <fail.h>

/* This tool is based on the assumption that a clean system shutdown
   after stopping all the services is exactly

         * umount -a
         * reboot(2)

   Init (well service supervisor) should spawn this tool when it's done.

   There's no other way to do umount -a in minitools, in particular
   mount -u cannot do that. This is because shutdown is the only situation
   when umount -a makes sense, and handling -a requires lots of code
   not used in any other umount mode (reading mountinfo etc.) */

#define OPTS "ph"
#define OPT_p (1<<0)	/* poweroff */
#define OPT_h (1<<1)	/* halt */

ERRTAG = "shutdown";
ERRLIST = {
	REPORT(EPERM), REPORT(ENOENT), REPORT(EBADF), REPORT(EFAULT),
	REPORT(ELOOP), REPORT(ENOMEM), REPORT(ENOTDIR), REPORT(EOVERFLOW),
	REPORT(EINTR), REPORT(EIO), REPORT(ENOSYS), REPORT(EISDIR),
	REPORT(EMFILE), REPORT(ENFILE), REPORT(EBUSY), RESTASNUMBERS
};

/* A line from mountinfo looks like this:

   66 21 8:3 / /home rw,noatime,nodiratime shared:25 - ext4 /dev/sda3 ...

   In this tool we are only interested in [4] mountpoint.
   We need to iterate over all of them -- in reverse order.

   Like most /proc files, mountinfo has 0 size so it's pointless to mmap it. */

char minbuf[4096]; /* mountinfo */

#define MP 5

static char* findline(char* p, char* e)
{
	while(p < e)
		if(*p == '\n')
			return p;
		else
			p++;
	return NULL;
}

static int splitline(char* line, char** parts, int n)
{
	char* p = line;
	char* q;
	int i;

	for(i = 0; i < n; i++) {
		if(!*(q = strcbrk(p, ' ')))
			break;
		parts[i] = p;
		*q = '\0'; p = q + 1;
	};

	return (i < n);
}

static void add_mountpoint(struct heap* hp, char* mp)
{
	int len = strlen(mp);
	char* buf = halloc(hp, len + 1);
	memcpy(buf, mp, len);
	buf[len] = '\0';
}

static int scan_mountpoints(struct heap* hp)
{
	const char* mountinfo = "/proc/self/mountinfo";
	long fd = xchk(sysopen(mountinfo, O_RDONLY), "cannot open", mountinfo);

	long rd;
	long of = 0;
	char* mp[MP];
	int count = 0;

	while((rd = sysread(fd, minbuf + of, sizeof(minbuf) - of)) > 0) {
		char* p = minbuf;
		char* e = minbuf + rd;
		char* q;

		while((q = findline(p, e))) {
			char* line = p; *q = '\0'; p = q + 1;

			if(splitline(line, mp, MP))
				continue;

			add_mountpoint(hp, mp[4]);
			count++;
		} if(p < e) {
			of = e - p;
			memcpy(minbuf, p, of);
		}
	}

	return count;
}

static char** index_scanned(struct heap* hp, int count)
{
	char** mps = halloc(hp, (count+1)*sizeof(char*));
	int i;

	char* brk = hp->brk;
	char* end = hp->ptr;
	char* p;
	int sep = 1;

	for(p = brk; p < end; p++) {
		if(sep && i < count)
			mps[i++] = p;
		sep = !*p;
	};

	return mps;
}

/* First try to umount, then try to remount r/o.

   The second attempt to umount is there to deal with improperly
   ordered entries, i.e. child dir umounted after parent.

   The entries in mountinfo follow mount order, so backward pass
   deals with most of that, *however* switchroot or pivot_root
   done after mounting /dev, /tmp and such messes up the order
   and requires exactly one extra pass to undo. */

static int umount1(char* mp)
{
	long ret = sysumount(mp, 0);

	if(!ret) return 1;

	if(ret == -EBUSY) return 0;

	warn("umount", mp, ret);

	return 1;
}

static void umount2(char* mp)
{
	int flags = MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC;
	long ret;

	ret = sysumount(mp, MNT_FORCE);

	if(!ret) return;

	warn("umount", mp, ret);

	ret = sysmount(NULL, mp, NULL, flags, NULL);

	if(!ret) return;

	warn("remount", mp, ret);
}

static void umountall(void)
{
	int i;

	struct heap hp;
	hinit(&hp, PAGE);

	int count = scan_mountpoints(&hp);
	char** mps = index_scanned(&hp, count);

	char* done = halloc(&hp, count*sizeof(char));

	for(i = count - 1; i >= 0; i--)
		done[i] = umount1(mps[i]);

	for(i = count - 1; i >= 0; i--)
		if(!done[i]) umount2(mps[i]);
}

int main(int argc, char** argv)
{
	int mode;
	int opts = 0;
	int i = 1;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts & OPT_p)
		mode = RB_POWER_OFF;
	else if(opts & OPT_h)
		mode = RB_HALT_SYSTEM;
	else
		mode = RB_AUTOBOOT;

	xchk(syssync(), "sync", NULL);

	umountall();

	xchk(sysreboot(mode), "reboot", NULL);

	return 0;
}
