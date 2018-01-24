#include <bits/ioctl/tty.h>

#include <sys/ioctl.h>

#include <errtag.h>
#include <util.h>

ERRTAG("ioctl");

int main(void)
{
	int ret, fd = STDIN;
	struct winsize ws;
	struct termios ts;

	if((ret = sys_ioctl(fd, TCGETS, &ts)) < 0)
		fail(NULL, "TCGETS", ret);
	if((ret = sys_ioctl(fd, TIOCGWINSZ, &ws)) < 0)
		fail(NULL, "TIOCGWINSZ", ret);

	return 0;
}
