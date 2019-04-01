#include <sys/file.h>
#include <sys/sched.h>

#include <string.h>

#include "passblk.h"
#include "passblk_term.h"

static struct {
	char* title;
	int len;
	int ptr;
	int left;
	int tlen;
	int wlen;
	char* buf;
	int ir, ic, iw;
	int show;
} inp;

static void repeat(char c, int n)
{
	char buf[10];
	int chunk = sizeof(buf);

	memset(buf, c, chunk);

	for(; n >= chunk; n -= chunk)
		output(buf, chunk);

	output(buf, n % chunk);
}

static int max(int a, int b)
{
	return a > b ? a : b;
}

static int min(int a, int b)
{
	return a < b ? a : b;
}

static void initialize(char* title, char* buf, ulong len)
{
	int tlen = strlen(title);
	int wlen = max(tlen, min(50, len));

	memzero(&inp, sizeof(inp));

	inp.title = title;
	inp.len = len - 1;
	inp.buf = buf;
	inp.tlen = tlen;
	inp.wlen = wlen;
}

static void prep_box(void)
{
	int wlen = inp.wlen;
	int tlen = inp.tlen;

	int bw = wlen + 6;
	int bh = 5;
	int bc = cols/2 - bw/2;
	int br = rows/2 - bh/2;

	drawbox(br, bc, bw, bh);

	int tr = rows/2 - 2;
	int tc = cols/2 - tlen/2 - 1;

	moveto(tr, tc);
	output(" ", 1);
	output(inp.title, tlen);
	output(" ", 1);

	int ic = cols/2 - wlen/2;
	int ir = rows/2;

	moveto(ir, ic);
	repeat('_', wlen);
	moveto(ir, ic);

	inp.ir = ir;
	inp.ic = ic;
	inp.iw = wlen;
}

static void show_tip(void)
{
	char* msg = "[Tab] toggle visibility";
	int len = strlen(msg);

	moveto(inp.ir + 4, cols/2 - len/2);
	output(msg, len);
	moveto(inp.ir, inp.ic);
}

static void hide_tip(void)
{
	moveto(inp.ir + 4, 1);
	erase_line();
}

static int visible_text_width(void)
{
	return inp.ptr - inp.left;
}

static void redraw_area(void)
{
	int vis = inp.ptr - inp.left;

	if(vis > inp.wlen)
		vis = inp.wlen;

	int pad = inp.wlen - vis;

	moveto(inp.ir, inp.ic + vis);
	repeat('_', pad);

	moveto(inp.ir, inp.ic);

	if(inp.show)
		output(inp.buf + inp.left, vis);
	else
		repeat('*', vis);
}

static void scroll_right(int count)
{
	if(count > inp.left)
		inp.left -= count;
	else
		inp.left = 0;
}

static void scroll_left(void)
{
	if(inp.left >= inp.ptr)
		return;

	inp.left += 1;
}

static int delete(void)
{
	char* b = inp.buf;
	char* p = inp.buf + inp.ptr;

	if(p <= b)
		return 0;

	int cw = 1;

	if(cw <= inp.ptr)
		inp.ptr -= cw;
	else
		inp.ptr = 0;

	if(inp.left && visible_text_width() < 5)
		scroll_right(10);

	redraw_area();

	return 0;
}

static int toggle(void)
{
	inp.show = !inp.show;

	redraw_area();

	return 0;
}

static int add_text(char* rbuf, int rlen)
{
	if(inp.ptr + rlen >= inp.len)
		return 0;

	memcpy(inp.buf + inp.ptr, rbuf, rlen);
	inp.ptr += rlen;

	if(visible_text_width() > inp.wlen)
		scroll_left();

	redraw_area();

	return 0;
}

static int esc_seq(char* rbuf, int rlen)
{
	(void)rbuf;
	(void)rlen;

	return 0;
}

static int ctl_code(char c)
{
	if(c == 0x08) /* Backspace */
		return delete();
	if(c == 0x09) /* Tab */
		return toggle();
	if(c == 0x0D) /* Esc */
		return 1;
	return 0;
}

static int handle(char* rbuf, int rlen)
{
	if(rlen <= 0)
		return 0;
	if(*rbuf == '\033')
		return esc_seq(rbuf, rlen);
	if(*rbuf < 0x20)
		return ctl_code(*rbuf);
	if(*rbuf == 0x7F)
		return delete();

	return add_text(rbuf, rlen);
}

int input(char* title, char* buf, int len)
{
	char rbuf[10];
	int ret;

	initialize(title, buf, len);
	prep_box();

	show_tip();

	while((ret = sys_read(STDIN, rbuf, sizeof(rbuf))) > 0)
		if(handle(rbuf, ret))
			break;

	buf[inp.ptr] = '\0';

	hide_tip();

	return inp.ptr;
}

void message(char* title, int ms)
{
	int wlen = strlen(title);

	int bw = wlen + 6;
	int bh = 5;
	int bc = cols/2 - bw/2;
	int br = rows/2 - bh/2;

	drawbox(br, bc, bw, bh);

	int ic = cols/2 - wlen/2;
	int ir = rows/2;

	moveto(ir, ic);
	outstr(title);
	moveto(ir, ic);

	struct timespec ts = { ms/1000, (ms % 1000)*1000*1000 };

	sys_nanosleep(&ts, NULL);
}
