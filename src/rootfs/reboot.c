#include <sys/file.h>
#include <sys/sync.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/sched.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include <heap.h>
#include <main.h>

/* This tool is based on the assumption that a clean system shutdown
   after stopping all the services is exactly

         * umount -a
         * reboot(2)

   svctl should spawn this tool when it's done.

   There's no other way to do umount -a in minitools, in particular
   kmount -u cannot do that. This is because shutdown is the only situation
   when umount -a makes sense, and handling -a requires lots of code
   not used in any other umount modes (reading mountinfo etc.) */

#define OPTS "ph"
#define OPT_p (1<<0)	/* poweroff */
#define OPT_h (1<<1)	/* halt */

ERRTAG("reboot");
ERRLIST(NEPERM NENOENT NEBADF NEFAULT NELOOP NENOMEM NENOTDIR NEOVERFLOW
	NEINTR NEIO NENOSYS NEISDIR NEMFILE NENFILE NEBUSY NEINVAL);

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

/* Attempts to umount root return -EINVAL, cluttering the output.
   The only op on / that is expected to succeed is r/o remount. */

int isroot(char* mp)
{
	return (mp[0] == '/' && !mp[1]);
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
	int ret;

	if(isroot(mp))
		return 0;
	if((ret = sys_umount(mp, 0)) >= 0)
		return 1;
	if(ret == -EBUSY)
		return 0;

	warn("umount", mp, ret);

	return 1;
}

static void umount2(char* mp)
{
	int flags = MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_NOEXEC;
	long ret;

	if(isroot(mp))
		;
	else if((ret = sys_umount(mp, MNT_FORCE)) >= 0)
		return;

	if((ret = sys_mount(NULL, mp, NULL, flags, NULL)) >= 0)
		return;

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

static noreturn void wait_child(int pid)
{
	int ret, status;

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		fail("waitpid", NULL, ret);

	if(status)
		fail("child process failed", NULL, 0);
	else
		fail("child process exited successfully", NULL, 0);
}

int main(int argc, char** argv)
{
	int mode = RB_AUTOBOOT;
	int i = 1, opts = 0, ret, pid;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);

	if(opts == OPT_p)
		mode = RB_POWER_OFF;
	else if(opts == OPT_h)
		mode = RB_HALT_SYSTEM;
	else if(opts)
		fail("cannot use -ph together", NULL, 0);
	else mode = RB_AUTOBOOT;

	if((ret = sys_sync()) < 0)
		warn("sync", NULL, ret);

	umountall();

	if(sys_getpid() != 1)
		;
	else if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);
	else if(pid == 0)
		wait_child(pid);

	if((ret = sys_reboot(mode)) < 0)
		fail("reboot", NULL, ret);

	return 0;
}
