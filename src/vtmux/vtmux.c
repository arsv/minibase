#include <bits/ioctl/vt.h>

#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/ioctl.h>

#include <format.h>
#include <util.h>
#include <main.h>

#include "vtmux.h"

char** environ;

int initialtty;
int primarytty;
int greetertty;
int activetty;
int lastusertty;

int tty0fd;
int switchlock;

ERRTAG("vtmux");

static int intarg(char* arg)
{
	char* p;
	int ret;

	if(!(p = parseint(arg, &ret)) || *p)
		fail("integer argument required", NULL, 0);

	return ret;
}

static void setup_tty0(void)
{
	int fd, flags = O_RDWR | O_NOCTTY | O_CLOEXEC;
	char* name = "/dev/tty0";

	if((fd = sys_open(name, flags)) < 0)
		fail("open", name, fd);

	tty0fd = fd;
}

int main(int argc, char** argv)
{
	int i = 1;

	environ = argv + argc + 1;

	if(i < argc)
		primarytty = intarg(argv[i++]);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	setup_tty0();
	setup_signals();
	setup_ctrl();
	scan_pinned();

	grab_initial_lock();
	switchto(primarytty);

	poll_inputs();

	terminate_children();
	restore_initial_tty();

	return 0;
}
