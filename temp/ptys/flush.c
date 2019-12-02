#include <bits/ioctl/tty.h>
#include <bits/ioctl/pty.h>

#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/proc.h>
#include <sys/sched.h>
#include <sys/splice.h>

#include <main.h>
#include <util.h>

ERRTAG("flush");

/* Quick sketch for PTY flush. When a detached client polls read-ready, 
   ptyhub needs to empty the fd but does not really need the data as such.

   So instead of doing a read and making the kernel copy stuff, the idea
   is to flush the buffer. Luckily, Linux has an ioctl exactly for that.

   Pity there's no ioctl for some sort of flush mode so we won't need to
   wake up ptyhub on any detached client activity.  */

static const char msg1[] = "Line 1\n";
static const char msg2[] = "Line 2\n";

static void recv_input(int mfd)
{
	char buf[1024];
	int ret;

	if((ret = sys_ioctli(mfd, TCFLSH, 0)) < 0)
		fail("ioctl", "TCFLSH", ret);

	if((ret = sys_read(mfd, buf, sizeof(buf))) < 0)
		fail("read", NULL, ret);
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	int mfd, sfd;
	int pid, status;
	int ret, lock = 0;
	struct timespec ts = { 1, 0 };

	if((mfd = sys_open("/dev/ptmx", O_RDWR)) < 0)
		fail("open", "/dev/ptmx", mfd);

	if((ret = sys_ioctl(mfd, TIOCSPTLCK, &lock)) < 0)
		fail("ioctl", "TIOCSPTLCK", ret);
	if((sfd = sys_ioctli(mfd, TIOCGPTPEER, O_RDWR | O_NOCTTY)) < 0)
		fail("ioctl", "TIOCGPTPEER", sfd);

	if((pid = sys_fork()) < 0)
		fail("fork", NULL, pid);

	if(pid == 0) {
		(void)sys_close(mfd);
		(void)sys_write(sfd, msg1, sizeof(msg1) - 1);
		(void)sys_nanosleep(&ts, NULL);
		(void)sys_write(sfd, msg2, sizeof(msg2) - 1);
		_exit(0);
	};

	recv_input(mfd);

	(void)sys_waitpid(pid, &status, 0);

	return 0;
}
