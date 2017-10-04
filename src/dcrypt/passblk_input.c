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

	int ir, ic, iw;

	char buf[120];
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
	return a > b ? b : a;
}

static void initialize(char* title, ulong len)
{
	if(len > sizeof(inp.buf))
		len = sizeof(inp.buf);

	int tlen = strlen(title);
	int wlen = min(cols - 30, max(tlen, len));

	memzero(&inp, sizeof(inp));

	inp.title = title;
	inp.len = len;
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

static int prev_utf_len(char* b, char* p)
{
	char* q = p;

	for(q--; q > b; q--)
		if((*q & 0xC0) != 0xC0)
			break;

	return p - q;
}

static int curr_utf_len(char* p)
{
	int c = *p;

	if(!(c & 0x80))
		return 1;
	if((c & 0xE0) == 0xC0)
		return 2;
	if((c & 0xF0) == 0xE0)
		return 3;
	if((c & 0xF8) == 0xF0)
		return 4;

	return 1;
}

static int glyph_width(char* p)
{
	return 1;
}

static int text_width(char* p, char* e)
{
	int w = 0;

	while(p < e) {
		w += glyph_width(p);
		p += curr_utf_len(p);
	}

	return w;
}

static int visible_text_width(void)
{
	char* s = inp.buf + inp.left;
	char* e = inp.buf + inp.ptr;

	return text_width(s, e);
}

static void redraw_area(void)
{
	int tw = 0;
	char* s = inp.buf + inp.left;
	char* p = s;
	char* e = inp.buf + inp.ptr;

	while(p < e) {
		int u = curr_utf_len(p);
		int w = glyph_width(p);

		if(tw + w > inp.wlen)
			break;

		tw += w;
		p += u;
	}

	int vw = text_width(s, p);

	moveto(inp.ir, inp.ic + vw);
	repeat('_', inp.wlen - vw);

	moveto(inp.ir, inp.ic);
	output(s, p - s);

}

static void scroll_right(void)
{
	char* b = inp.buf;
	char* p = inp.buf + inp.left;

	if(p <= b)
		return;

	int bw = 0;
	int tw = visible_text_width();

	while(p > b) {
		int u = prev_utf_len(b, p);
		p -= u;
		int w = glyph_width(p);

		if(tw + w > inp.wlen)
			break;

		bw += u;
		tw += w;
	}

	if(bw < inp.left)
		inp.left -= bw;
	else
		inp.left = 0;
}

static void scroll_left(void)
{
	if(inp.left >= inp.ptr)
		return;
	inp.left += curr_utf_len(inp.buf + inp.ptr);
}

static int delete(void)
{
	char* b = inp.buf;
	char* p = inp.buf + inp.ptr;

	if(p <= b)
		return 0;

	int cw = prev_utf_len(b, p);

	if(cw <= inp.ptr)
		inp.ptr -= cw;
	else
		inp.ptr = 0;

	if(inp.left && visible_text_width() < 3)
		scroll_right();

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
	return 0;
}

static int ctl_code(char c)
{
	if(c == 13)
		return 1;
	if(c == 8)
		return delete();
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

	initialize(title, len);
	prep_box();

	while((ret = sys_read(STDIN, rbuf, sizeof(rbuf))) > 0)
		if(handle(rbuf, ret))
			break;

	uint ptr = inp.ptr;

	if(ptr > sizeof(inp.buf))
		ptr = sizeof(inp.buf);

	memcpy(buf, inp.buf, ptr);
	buf[ptr] = '\0';
	memzero(&inp, sizeof(inp));

	return ptr;
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
