#include <bits/ioctl/tty.h>

#include <sys/file.h>
#include <sys/proc.h>
#include <sys/creds.h>
#include <sys/sched.h>
#include <sys/ioctl.h>

#include <string.h>
#include <format.h>
#include <util.h>
#include <main.h>

ERRTAG("rootsh");

static int open_dev(char* tty)
{
	int fd;

	FMTBUF(p, e, path, 100);
	p = fmtstr(p, e, "/dev/");
	p = fmtstr(p, e, tty);
	FMTEND(p, e);

	if((fd = sys_open(path, O_RDWR | O_CLOEXEC)) < 0)
		fail(NULL, path, fd);

	return fd;
}

static int child(int fd, char** envp)
{
	int ret;

	sys_dup2(fd, 0);
	sys_dup2(fd, 1);
	sys_dup2(fd, 2);

	if((ret = sys_setsid()) < 0)
		fail("setsid", NULL, ret);
	if((ret = sys_ioctl(STDOUT, TIOCSCTTY, 0)) < 0)
		fail("ioctl", "TIOCSCTTY", ret);

	char* argv[] = { "cmd", NULL };

	ret = execvpe(*argv, argv, envp);

	fail(NULL, *argv, ret);
}

static void sleep(int sec)
{
	struct timespec ts = { sec, 0 };

	sys_nanosleep(&ts, NULL);
}

static void spawn_shell(int fd, char** envp)
{
	int pid, ret, status;

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);
	if(pid == 0)
		_exit(child(fd, envp));

	if((ret = sys_waitpid(pid, &status, 0)) < 0)
		fail("waitpid", NULL, ret);

	sleep(1);
}

int main(int argc, char** argv)
{
	char** envp = argv + argc + 1;

	if(argc < 2)
		fail("too few arguments", NULL, 0);
	if(argc > 3)
		fail("too many arguments", NULL, 0);

	char* tty = argv[1];

	if(strncmp(tty, "tty", 3))
		fail("refusing to run on", tty, 0);

	int fd = open_dev(tty);

	while(1) spawn_shell(fd, envp);
}
