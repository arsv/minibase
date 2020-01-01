#include <bits/ioctl/tty.h>
#include <sys/ioctl.h>
#include <sys/file.h>
#include <sys/ppoll.h>
#include <sys/signal.h>

#include <main.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <sigset.h>
#include <util.h>

ERRTAG("keys");

#define CSI "\033["

struct top {
	int sigfd;
	struct pollfd pfds[2];
	struct winsize ws;
	struct termios ts;
};

#define CTX struct top* ctx

static const struct key {
	short code;
	char name[6];
} keys[] = {
	{ '\033', "ESC"   },
	{ ' ',    "SPACE" },
};

static const char* keyname(int key)
{
	const struct key* k;

	for(k = keys; k < keys + ARRAY_SIZE(keys); k++)
		if(k->code == key)
			return k->name;

	return NULL;
}

static void output(char* s, int len)
{
	sys_write(STDOUT, s, len);
}

static void outstr(char* s)
{
	output(s, strlen(s));
}

static void moveto(int r, int c)
{
	char buf[20];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, CSI);
	p = fmtint(p, e, r);
	p = fmtstr(p, e, ";");
	p = fmtint(p, e, c);
	p = fmtstr(p, e, "H");

	output(buf, p - buf);
}

static void makeline(char* l, char* m, char* r, int n)
{
	int i;

	outstr(l);
	for(i = 1; i < n - 1; i++)
		outstr(m);
	outstr(r);
}

static void message(CTX, char* msg)
{
	int len = strlen(msg);
	int rows = ctx->ws.row;
	int cols = ctx->ws.col;

	int mw = (len < 20 ? 20 : len);
	int ww = mw + 6;
	int mc = cols/2 - len/2;

	int wc = cols/2 - mw/2 - 3;
	int wr = rows/2 - 2;

	moveto(wr, wc);
	makeline("┌", "─", "┐", ww);
	moveto(wr + 1, wc);
	makeline("│", " ", "│", ww);
	moveto(wr + 2, wc);
	makeline("│", " ", "│", ww);
	moveto(wr + 3, wc);
	makeline("│", " ", "│", ww);
	moveto(wr + 4, wc);
	makeline("└", "─", "┘", ww);

	moveto(wr + 2, mc);
	outstr(msg);
}

static void prep_cursor(CTX)
{
	int rows = ctx->ws.row;
	int cols = ctx->ws.col;

	int wr = rows/2 + 5;
	int wc = cols/2 - 10;

	moveto(wr, wc);
}

static void show_input(CTX, void* buf, int len)
{
	int rows = ctx->ws.row;
	int cols = ctx->ws.col;

	unsigned char* v = buf;
	char out[10];
	int wr = rows/2 + 5;
	int wc = cols/2 - 10;
	int i;

	moveto(wr, wc);
	outstr(CSI "K");

	for(i = 0; i < len; i++) {
		char* p = out;
		char* e = out + sizeof(out) - 1;
		const char* name;

		if((name = keyname(v[i]))) {
			p = fmtstr(p, e, (char*)name);
		} else if(v[i] > 0x20 && v[i] < 0x7F) {
			p = fmtchar(p, e, v[i]);
		} else {
			p = fmtstr(p, e, "0x");
			p = fmtbyte(p, e, v[i]);
		}

		p = fmtstr(p, e, " ");

		output(out, p - out);
	};
}

static void init_terminal(CTX)
{
	int ret, fd = STDIN;
	struct termios ts;

	if((ret = sys_ioctl(fd, TCGETS, &ctx->ts)) < 0)
		fail("ioctl", "TCGETS", ret);

	memcpy(&ts, &ctx->ts, sizeof(ts));

	ts.iflag &= ~(IGNPAR | ICRNL | IXON | IMAXBEL);
	ts.oflag &= ~(OPOST | ONLCR);
	ts.lflag &= ~(ICANON | ECHO);

	if((ret = sys_ioctl(0, TCSETS, &ts)) < 0)
		fail("ioctl", "TCSETS", ret);
}

static void fini_terminal(CTX)
{
	int ret, fd = STDIN;
	int rows = ctx->ws.row;

	moveto(rows, 1);

	if((ret = sys_ioctl(fd, TCSETS, &ctx->ts)) < 0)
		fail("ioctl", "TCSETS", ret);
}

static void quit(CTX, const char* msg, char* arg, int err)
{
	fini_terminal(ctx);
	fail(msg, arg, err);
}

static void redraw(CTX)
{
	int ret, fd = STDIN;

	if((ret = sys_ioctl(fd, TIOCGWINSZ, &ctx->ws)) < 0)
		fail("ioctl", "TIOCGWINSZ", ret);

	moveto(1,1);
	outstr(CSI "J");

	message(ctx, "Tap keys to see their event codes. Press C-d when done.");
	prep_cursor(ctx);
}

static void check_ctrl_d(CTX, char* buf, int len)
{
	char* p = buf;
	char* e = buf + len;

	for(; p < e; p++)
		if(*p == 4)
			goto got;
	return;
got:
	fini_terminal(ctx);
	_exit(0x00);
}

static void check_terminp(CTX)
{
	int ret, fd = STDIN;
	char buf[128];

	if((ret = sys_read(fd, buf, sizeof(buf))) < 0)
		fail("read", "stdin", ret);

	show_input(ctx, buf, ret);

	check_ctrl_d(ctx, buf, ret);
}

static void check_signals(CTX)
{
	int ret, fd = ctx->sigfd;
	struct siginfo si;

	if((ret = sys_read(fd, &si, sizeof(si))) < 0)
		quit(ctx, "read", "signalfd", 0);

	int sig = si.signo;

	if(sig == SIGWINCH)
		redraw(ctx);
	else
		quit(ctx, "signalled", NULL, 0);
}

static void poll(CTX)
{
	int ret;
	struct pollfd* pfds = ctx->pfds;
	int npfds = 2;

	if((ret = sys_ppoll(pfds, npfds, NULL, NULL)) < 0)
		quit(ctx, "poll", NULL, ret);
	if(!ret) return;

	if(pfds[0].revents & POLLIN)
		check_terminp(ctx);
	if(pfds[1].revents & POLLIN)
		check_signals(ctx);

	if(pfds[0].revents & ~POLLIN)
		quit(ctx, "lost", "stdin", 0);
	if(pfds[1].revents & ~POLLIN)
		quit(ctx, "lost", "signalfd", 0);
}

static void init_pollfds(CTX)
{
	int fd, ret;
	struct sigset mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGWINCH);
	sigaddset(&mask, SIGINT);
	sigaddset(&mask, SIGTERM);

	if((fd = sys_signalfd(-1, &mask, 0)) < 0)
		quit(ctx, "signalfd", NULL, fd);
	if((ret = sys_sigprocmask(SIG_SETMASK, &mask, NULL)) < 0)
		quit(ctx, "sigprocmask", NULL, ret);

	struct pollfd* pfds = ctx->pfds;

	pfds[0].fd = STDIN;
	pfds[0].events = POLLIN;
	pfds[1].fd = fd;
	pfds[1].events = POLLIN;

	ctx->sigfd = fd;
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;

	(void)argc;
	(void)argv;

	memzero(ctx, sizeof(*ctx));

	init_terminal(ctx);
	init_pollfds(ctx);

	redraw(ctx);

	while(1) poll(ctx);
}
