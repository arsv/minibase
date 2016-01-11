#include <bits/errno.h>
#include <bits/fcntl.h>
#include <sys/ioctl.h>
#include <sys/open.h>
#include <sys/close.h>

#include <argbits.h>
#include <parseint.h>
#include <fail.h>
#include <null.h>

#define KDGKBTYPE	0x4B33
#define VT_ACTIVATE	0x5606
#define VT_WAITACTIVE	0x5607
#define VT_DISALLOCATE	0x5608

#define OPTS "d"
#define OPT_d (1<<0)

ERRTAG = "chvt";
ERRLIST = { 
	REPORT(EBADF), REPORT(EFAULT), REPORT(EINVAL), REPORT(ENOTTY),
	REPORT(EACCES), REPORT(EISDIR), REPORT(ELOOP), REPORT(EMFILE),
	REPORT(ENFILE), REPORT(ENODEV), REPORT(ENOENT), REPORT(ENOMEM),
	REPORT(ENOTDIR), REPORT(EPERM), REPORT(EROFS), REPORT(EBUSY),
	RESTASNUMBERS
};

static long openconsole(const char* name)
{
	long io;
	long fd;
	char arg = 0;

	if((fd = sysopen(name, O_RDWR)) < 0)
		return fd;

	if((io = sysioctl(fd, KDGKBTYPE, (long)&arg)) >= 0)
		return fd;

	sysclose(fd);
	return io;
}

static long consolefd(void)
{
	long ret;

	if((ret = openconsole("/dev/tty")) >= 0)
		return ret;

	if((ret = openconsole("/dev/tty0")) >= 0)
		return ret;

	fail("cannot find console device", NULL, 0);
}

static void chvt(long cfd, int vt, char* vtname)
{
	xchk(sysioctl(cfd, VT_ACTIVATE, vt),
		"ioctl VT_ACTIVATE", vtname);
	xchk(sysioctl(cfd, VT_WAITACTIVE, vt),
		"ioctl VT_WAITACTIVE", vtname);
}

static void rmvt(long cfd, int vt, char* vtname)
{
	xchk(sysioctl(cfd, VT_DISALLOCATE, vt),
		"ioctl VT_DISALLOCATE", vtname);
}

static int xatoi(char* str)
{
	int n;
	char* p;
	
	if(!(p = parseint(str, &n)) || *p)
		fail("not a number", str, 0);

	return n;
}

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++]+1);

	long cfd = consolefd();

	if(!(opts & OPT_d)) {
		if(i >= argc)
			fail("need console number to switch to", NULL, 0);
		if(i < argc - 1)
			fail("too many arguments", NULL, 0);

		chvt(cfd, xatoi(argv[i]), argv[i]);
	} else if(i < argc) {
		for(; i < argc; i++)
			rmvt(cfd, xatoi(argv[i]), argv[i]);
	} else {
		rmvt(cfd, 0, "unused");
	}

	return 0;
}
