#include <bits/ioctl/tty.h>
#include <sys/file.h>
#include <sys/poll.h>
#include <sys/ioctl.h>

#include <format.h>
#include <fail.h>
#include <exit.h>

#include "vtmux.h"

char** environ;

int initialtty;
int primarytty;
int greetertty;
int activetty;

ERRTAG = "vtmux";
ERRLIST = {
	REPORT(EINVAL), REPORT(ENOENT), REPORT(ENOTDIR), REPORT(EFAULT),
	REPORT(ENOTTY), REPORT(EINTR), REPORT(ENOSYS), REPORT(EIO),
	REPORT(EPERM), REPORT(EACCES), REPORT(ENOTSOCK), REPORT(EADDRINUSE),
	REPORT(EBADF),
	RESTASNUMBERS
};

static int intarg(char* arg)
{
	char* p;
	int ret;

	if(!(p = parseint(arg, &ret)) || *p)
		fail("integer argument required", NULL, 0);

	return ret;
}

int main(int argc, char** argv, char** envp)
{
	int i = 1;

	environ = envp;

	if(i < argc)
		primarytty = intarg(argv[i++]);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	setup_signals();
	setup_ctrl();
	scan_pinned();

	grab_initial_lock();
	switchto(primarytty);

	poll_inputs();

	terminate_children();
	restore_initial_tty();
	clear_ctrl();

	return 0;
}
