#include <bits/ioctl/vt.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <printf.h>
#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("ttyinfo");

int open_tty_device(int tty)
{
	char namebuf[30];

	char* p = namebuf;
	char* e = namebuf + sizeof(namebuf) - 1;

	p = fmtstr(p, e, "/dev/tty");
	p = fmtint(p, e, tty);
	*p++ = '\0';

	int fd = sys_open(namebuf, O_RDWR | O_CLOEXEC);

	if(fd < 0)
		warn("open", namebuf, fd);

	return fd;
}

static const char* kdmode(int i)
{
	if(i == 0)
		return "TEXT";
	if(i == 1)
		return "GRAPH";

	return "???";
}

static const char* vtmode(int i)
{
	if(i == 0)
		return "AUTO";
	if(i == 1)
		return "PROCESS";
	if(i == 2)
		return "ACKACQ";
	return "???";
}

static void ttyinfo(int tty, int active)
{
	int fd, ret;
	struct vt_mode vtm;
	int mode;

	if((fd = open_tty_device(tty)) < 0)
		return;

	if((ret = sys_ioctl(fd, VT_GETMODE, &vtm)) < 0)
		return;
	if((ret = sys_ioctl(fd, KDGETMODE, &mode)) < 0)
		return;

	tracef("tty%i%s %s %s\n", tty, active ? "*": " ",
			kdmode(mode), vtmode(vtm.mode));
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;

	int fd, ret, i;
	struct vt_stat vst;

	if((fd = open_tty_device(0)) < 0)
		return -1;

	if((ret = sys_ioctl(fd, VT_GETSTATE, &vst)) < 0)
		return -1;


	for(i = 1; i < 32; i++)
		if(vst.state & (1 << i))
			ttyinfo(i, i == vst.active);

	sys_ioctl(fd, VT_DISALLOCATE, NULL);

	return 0;
}
