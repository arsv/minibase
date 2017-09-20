#include <sys/file.h>
#include <sys/sync.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/sched.h>

#include <errtag.h>
#include <string.h>
#include <util.h>
#include <heap.h>

/* This tool is based on the assumption that a clean system shutdown
   after stopping all the services is exactly

         * umount -a
         * reboot(2)

   svcmon should spawn this tool when it's done.

   There's no other way to do umount -a in minitools, in particular
   mount -u cannot do that. This is because shutdown is the only situation
   when umount -a makes sense, and handling -a requires lots of code
   not used in any other umount modes (reading mountinfo etc.) */

#define OPTS "phr"
#define OPT_p (1<<0)	/* poweroff */
#define OPT_h (1<<1)	/* halt */
#define OPT_r (1<<2)	/* reboot */

ERRTAG("reboot");
ERRLIST(NEPERM NENOENT NEBADF NEFAULT NELOOP NENOMEM NENOTDIR NEOVERFLOW
	NEINTR NEIO NENOSYS NEISDIR NEMFILE NENFILE NEBUSY);

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
	int fd;

	if((fd = sys_open(mountinfo, O_RDONLY)) < 0) {
		warn(NULL, mountinfo, fd);
		return 0;
	}

	long rd;
	long of = 0;
	char* mp[MP];
	int count = 0;

	while((rd = sys_read(fd, minbuf + of, sizeof(minbuf) - of)) > 0) {
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
	int i = 0;

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
	long ret = sys_umount(mp, 0);

	if(!ret) return 1;

	if(ret == -EBUSY) return 0;

	warn("umount", mp, ret);

	return 1;
}

static void umount2(char* mp)
{
	int flags = MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC;
	long ret;

	ret = sys_umount(mp, MNT_FORCE);

	if(!ret) return;

	warn("umount", mp, ret);

	ret = sys_mount(NULL, mp, NULL, flags, NULL);

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

static void sleep(int sec)
{
	struct timespec ts = { sec, 0 };

	sys_nanosleep(&ts, NULL);
}

static void warn_pause(void)
{
	warn("waiting 5 seconds", NULL, 0);
	sleep(5);
}

static int spawn_reboot(int mode)
{
	int pid, ret, status;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0) {
		if((ret = sys_reboot(mode)) < 0)
			warn("reboot", NULL, ret);
		return (ret < 0 ? 0xFF : 0x00);
	} else {
		if((ret = sys_waitpid(pid, &status, 0)) < 0)
			fail("waitpid", NULL, ret);

		return (status ? 0xFF : 0x00);
	}
}

int main(int argc, char** argv)
{
	int mode = RB_AUTOBOOT;
	int i = 1, opts = 0, ret;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if((ret = sys_getpid()) != 1)
		fail("invoked with pid", NULL, ret);

	if(opts == OPT_p)
		mode = RB_POWER_OFF;
	else if(opts == OPT_h)
		mode = RB_HALT_SYSTEM;
	else if(opts == OPT_r)
		mode = RB_AUTOBOOT;
	else if(opts)
		fail("cannot use -phr together", NULL, 0);
	else warn_pause();

	warn("proceeding to sync and umount", NULL, 0);

	if((ret = sys_sync()) < 0)
		warn("sync", NULL, ret);

	umountall();

	warn("spawning child to stop the kernel", NULL, 0);

	return spawn_reboot(mode);
}
