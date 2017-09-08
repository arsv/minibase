#include <bits/ioctl/tty.h>
#include <sys/ioctl.h>
#include <sys/file.h>

#include <errtag.h>
#include <printf.h>
#include <format.h>
#include <string.h>
#include <util.h>

ERRTAG("keys");

#define CSI "\033["

int rows;
int cols;

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

static void message(char* msg)
{
	int len = strlen(msg);

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

static void clear(void)
{
	moveto(1,1);
	outstr(CSI "J");
}

static void prep_cursor(void)
{
	int wr = rows/2 + 5;
	int wc = cols/2 - 10;
	moveto(wr, wc);
}

static void show_input(void* buf, int len)
{
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

static int got_ctrl_d(char* buf, int len)
{
	char* p = buf;
	char* e = buf + len;

	for(; p < e; p++)
		if(*p == 4)
			return 1;

	return 0;
}

int main(void)
{
	struct winsize ws;
	struct termios ts, tso;
	int ret;
	char buf[100];

	if((ret = sys_ioctl(0, TIOCGWINSZ, &ws)) < 0)
		fail("ioctl", "TIOCGWINSZ", ret);

	rows = ws.row;
	cols = ws.col;

	if((ret = sys_ioctl(0, TCGETS, &ts)) < 0)
		fail("ioctl", "TCGETS", ret);

	memcpy(&tso, &ts, sizeof(ts));
	ts.iflag &= ~(IGNPAR | ICRNL | IXON | IMAXBEL);
	ts.oflag &= ~(OPOST | ONLCR);
	ts.lflag &= ~(ICANON | ECHO);

	if((ret = sys_ioctl(0, TCSETS, &ts)) < 0)
		fail("ioctl", "TCSETS", ret);

	clear();
	message("Tap keys to see their event codes. Press C-d when done.");
	prep_cursor();

	while((ret = sys_read(STDIN, buf, sizeof(buf))) > 0) {
		show_input(buf, ret);

		if(got_ctrl_d(buf, ret))
			break;
	}

	moveto(ws.row, 1);

	if((ret = sys_ioctl(0, TCSETS, &tso)) < 0)
		fail("ioctl", "TCSETS", ret);

	return 0;
}
