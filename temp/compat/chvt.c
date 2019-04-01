#include <sys/file.h>
#include <sys/ioctl.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

#define KDGKBTYPE	0x4B33
#define VT_ACTIVATE	0x5606
#define VT_WAITACTIVE	0x5607
#define VT_DISALLOCATE	0x5608

#define OPTS "d"
#define OPT_d (1<<0)

ERRTAG("chvt");
ERRLIST(NEBADF NEFAULT NEINVAL NENOTTY NEACCES NEISDIR NELOOP NEMFILE
	NENFILE NENODEV NENOENT NENOMEM NENOTDIR NEPERM NEROFS NEBUSY);

static long openconsole(const char* name)
{
	long io;
	long fd;
	char arg = 0;

	if((fd = sys_open(name, O_RDWR)) < 0)
		return fd;

	if((io = sys_ioctl(fd, KDGKBTYPE, &arg)) >= 0)
		return fd;

	sys_close(fd);
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
	int ret;

	if((ret = sys_ioctli(cfd, VT_ACTIVATE, vt)) < 0)
		fail("ioctl VT_ACTIVATE", vtname, ret);
	if((ret = sys_ioctli(cfd, VT_WAITACTIVE, vt)) < 0)
		fail("ioctl VT_WAITACTIVE", vtname, ret);
}

static void rmvt(long cfd, int vt, char* vtname)
{
	int ret;

	if((ret = sys_ioctli(cfd, VT_DISALLOCATE, vt)) < 0)
		fail("ioctl VT_DISALLOCATE", vtname, ret);
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
