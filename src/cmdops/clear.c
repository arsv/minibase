#include <bits/ioctl/tty.h>
#include <sys/file.h>
#include <sys/ioctl.h>

#include <errtag.h>
#include <util.h>

#define ctrl(c) ((c) & 0x1F)

ERRTAG("clear");
ERRLIST(NEINVAL NEBADF NENOTTY);

static const struct termios sane = {
	.iflag = BRKINT | ICRNL | IMAXBEL | IUTF8,
	.oflag = OPOST | ONLCR | NL0 | CR0 | TAB0 | BS0 | VT0 | FF0,
	.cflag = CREAD,
	.lflag = ISIG | ICANON | IEXTEN | ECHO | ECHOE | ECHOK \
	         | ECHOCTL | ECHOKE,
	.line = '\0',
	.cc = {
		[VINTR] = ctrl('c'),
		[VQUIT] = 28,
		[VERASE] = 127,
		[VKILL] = ctrl('u'),
		[VEOF] = ctrl('d'),
		[VTIME] = 0,
		[VMIN] = 0,
		[VSWTC] = 0,
		[VSTART] = ctrl('q'),
		[VSTOP] = ctrl('s'),
		[VSUSP] = ctrl('z'),
		[VEOL] = 0,
		[VREPRINT] = ctrl('r'),
		[VDISCARD] = 0,
		[VWERASE] = ctrl('w'),
		[VLNEXT] = ctrl('v'),
		[VEOL2] = 0
	}
};

#define ESC "\x1B"
#define CSI "\x1B["

static const char reset[] =
	CSI "H"		/* move cursor to origin (1,1) */
	CSI "J"		/* erase display, from cursor */
	CSI "0m"	/* reset attributes */
	ESC "c"		/* reset */
	CSI "?25h"	/* make cursor visible */
	ESC "(B";	/* select G0 charset */

#define OPTS "r"
#define OPT_r (1<<0)	/* reset tty */

int main(int argc, char** argv)
{
	int i = 1;
	int opts = 0;

	if(i < argc && argv[i][0] == '-')
		opts = argbits(OPTS, argv[i++] + 1);
	if(i < argc)
		fail("too many arguments", NULL, 0);

	sys_write(1, reset, sizeof(reset) - 1);

	if(opts & OPT_r)
		xchk(sys_ioctl(1, TCSETSW, (void*)&sane),
			"ioctl TCSETSW", "stdout");

	return 0;
}
