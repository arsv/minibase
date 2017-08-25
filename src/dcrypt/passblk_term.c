#include <bits/ioctl/tty.h>
#include <sys/ioctl.h>
#include <sys/file.h>

#include <format.h>
#include <string.h>
#include <util.h>

#include "passblk.h"
#include "passblk_term.h"

/* Doing this all *correctly* would require ncurses-like windowing system
   complete with an event loop and double buffering to allow redraws on C-l.
   However, this tool will run in a very particular environment, its tasks
   are limited, and it's a temporarty solution anyway until the KMS version
   arrives. So let's cut about as much corners as we can and make it into
   a sort of simplified dialog(1).
 
   Note random kernel messages *will* mess thing up, so it makes a lot of
   sense to switch VTs just to run this. */

#define CSI "\033["

int rows;
int cols;
int initialized;

struct box {
	int r, c, w, h;
} box;

static struct termios tso;

void term_init(void)
{
	struct termios ts;
	struct winsize ws;
	int ret;

	if((ret = sys_ioctl(0, TIOCGWINSZ, &ws)) < 0)
		fail("ioctl", "TIOCGWINSZ", ret);

	rows = ws.row;
	cols = ws.col;

	if((ret = sys_ioctl(0, TCGETS, &ts)) < 0)
		fail("ioctl", "TCGETS", ret);

	memcpy(&tso, &ts, sizeof(ts));
	ts.iflag |= IUTF8;
	ts.iflag &= ~(IGNPAR | ICRNL | IXON | IMAXBEL);
	ts.oflag &= ~(OPOST | ONLCR);
	ts.lflag &= ~(ICANON | ECHO);

	if((ret = sys_ioctl(0, TCSETS, &ts)) < 0)
		fail("ioctl", "TCSETS", ret);

	initialized = 1;
}

void term_back(void)
{
	struct termios ts;
	int ret;

	memcpy(&ts, &tso, sizeof(ts));

	ts.iflag |= IUTF8;
	ts.iflag &= ~(IGNPAR | ICRNL | IXON | IMAXBEL);
	ts.oflag &= ~(OPOST | ONLCR);
	ts.lflag &= ~(ICANON | ECHO);

	if((ret = sys_ioctl(0, TCSETS, &ts)) < 0)
		fail("ioctl", "TCSETS", ret);

	initialized = 1;
}

void term_fini(void)
{
	int ret;

	if(!initialized)
		return;

	park_cursor();
	show_cursor();

	if((ret = sys_ioctl(0, TCSETS, &tso)) < 0)
		fail("ioctl", "TCSETS", ret);

	initialized = 0;
}

void output(char* s, int len)
{
	sys_write(STDOUT, s, len);
}

void outstr(char* s)
{
	output(s, strlen(s));
}

static void tcs(char* csi, int n, int m, char c)
{
	char buf[20];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	p = fmtstr(p, e, csi);

	if(n)
		p = fmtint(p, e, n);
	if(n && m)
		p = fmtstr(p, e, ";");
	if(m)
		p = fmtint(p, e, m);

	p = fmtchar(p, e, c);
	*p = '\0';

	output(buf, p - buf);
}

void clear(void)
{
	hide_cursor();
	moveto(1,1);
	tcs(CSI, 0, 0, 'J');
}

void spaces(int n)
{
	tcs(CSI, n, 0, '@');
}

void moveto(int r, int c)
{
	tcs(CSI, r, c, 'H');
}

void scrollreg(int rf, int rt)
{
	tcs(CSI, rf, rt, 'r');
}

void park_cursor(void)
{
	moveto(rows, 1);
}

void hide_cursor(void)
{
	tcs(CSI "?", 25, 0, 'l');
}

void show_cursor(void)
{
	tcs(CSI "?", 25, 0, 'h');
}

static void makeline(char* l, char* m, char* r, int n)
{
	int i;

	outstr(l);
	for(i = 1; i < n - 1; i++)
		outstr(m);
	outstr(r);
}

void drawbox(int r, int c, int w, int h)
{
	int i = 0;

	clearbox();

	moveto(r + i, c);
	makeline("┌", "─", "┐", w);

	for(i++; i < h - 1; i++) {
		moveto(r + i, c);
		makeline("│", " ", "│", w);
	}

	moveto(r + i, c);
	makeline("└", "─", "┘", w);

	box.c = c; box.r = r;
	box.w = w; box.h = h;
}

void clearbox(void)
{
	int r = box.r, c = box.c;
	int h = box.h, w = box.w;
	int i;

	if(!w) return;

	for(i = 0; i < h; i++) {
		moveto(r + i, c);
		tcs(CSI, 2, 0, 'K');
	}

	memzero(&box, sizeof(box));
}

void status(char* msg)
{
	int len = strlen(msg);

	int mw = (len < 20 ? 20 : len);
	int mc = cols/2 - len/2;
	int mr = rows/2;

	int bw = mw + 6;
	int bh = 1 + 4;
	int bc = cols/2 - bw/2;
	int br = mr - bh/2;

	drawbox(br, bc, bw, bh);

	moveto(mr, mc);
	outstr(msg);

	park_cursor();
}
