#include <sys/file.h>
#include <sys/fpath.h>
#include <sys/ioctl.h>
#include <sys/signal.h>

#include <errtag.h>
#include <format.h>
#include <string.h>
#include <printf.h>
#include <sigset.h>
#include <util.h>

#include "cmd.h"
#include "unicode.h"

static void enter_term(CTX);
static void leave_term(CTX);

/* Input handling */

static void flush(CTX)
{
	if(!ctx->outptr)
		return;

	writeall(STDOUT, ctx->outbuf, ctx->outptr);

	ctx->outptr = 0;
}

static void outraw(CTX, char* buf, int len)
{
	if(len + ctx->outptr > ctx->outlen)
		flush(ctx);

	if(len > ctx->outlen) {
		writeall(STDOUT, buf, len);
	} else {
		memcpy(ctx->outbuf + ctx->outptr, buf, len);
		ctx->outptr += len;
	}
}

static void outbuf(CTX, int from, int to)
{
	/* TODO: range checks */
	outraw(ctx, ctx->buf + from, to - from);
}

static void outstr(CTX, char* str)
{
	outraw(ctx, str, strlen(str));
}

static void outcsi(CTX, int a, int b, char k)
{
	FMTBUF(p, e, buf, 30);
	p = fmtstr(p, e, "\033[");
	if(a) p = fmtint(p, e, a);
	if(a && b) p = fmtstr(p, e, ";");
	if(b) p = fmtint(p, e, b);
	p = fmtchar(p, e, k);
	FMTEND(p, e);

	outraw(ctx, buf, p - buf);
}

static void redraw_flush(CTX)
{
	int show = ctx->show;
	int ends = ctx->ends;
	int cur = ctx->cur;

	if(!ctx->redraw)
		return;

	ctx->redraw = 0;

	outcsi(ctx, 1, 0, 'G');
	outcsi(ctx, 0, 0, 'K');

	if(cur >= show && cur < ends) {
		outbuf(ctx, show, cur);
		outstr(ctx, "\033[s");
		outbuf(ctx, cur, ends);
		outstr(ctx, "\033[u");
	} else {
		outbuf(ctx, show, ends);
	}

	flush(ctx);
}

static void reviswi(CTX)
{
	char* buf = ctx->buf + ctx->show;
	long len = ctx->ends - ctx->show;

	ctx->viswi = visual_width(buf, len);
}

static void set_scroll_window(CTX, int pos)
{
	if(pos < 0)
		pos = 0;
	if(pos > ctx->ptr)
		pos = ctx->ptr;

	ctx->show = pos;

	int maxw = ctx->cols - 2;
	char* buf = ctx->buf + pos;
	int len = ctx->ptr - pos;
	long skip = skip_right_visually(buf, len, maxw);

	ctx->ends = pos + skip;
	ctx->viswi = visual_width(buf, skip);
}

static void set_scroll_redraw(CTX, int pos)
{
	set_scroll_window(ctx, pos);
	ctx->redraw = 1;
}

static void scroll_left(CTX)
{
	int maxw = ctx->cols - 2;
	char* buf = ctx->buf;
	int cur = ctx->cur;
	long skip = skip_left_visually(buf, cur, maxw - 2);

	set_scroll_redraw(ctx, cur - skip);
}

static void scroll_right(CTX)
{
	char* buf = ctx->buf;
	int cur = ctx->cur;
	long skip = skip_left_visually(buf, cur, 3);

	if(skip <= 0)
		return;
	if(skip > cur)
		return;

	set_scroll_redraw(ctx, cur - skip);
}

/* Fragment ctx->buf[cur:len] has been added, it fits on the screen
   and there's no tail to worry about. */

static void add_immediate(CTX, int cur, int len)
{
	outbuf(ctx, cur, cur + len);
}

/* Fragment has been added, and there's a tail but it's too short to
   hit the right margin. */

static void add_then_tail(CTX, int cur, int len, int tlen)
{
	outbuf(ctx, cur, cur + len);
	outstr(ctx, "\033[s");
	outbuf(ctx, cur + len, cur + len + tlen);
	outstr(ctx, "\033[K\033[u");

	reviswi(ctx);
}

/* Fragment ctx->buf[cur:len] has been added, and there's a tail that whould
   go over the margin unless trimmed to at most vtail positions. */

static void add_trim_tail(CTX, int cur, int len, int vtail)
{
	int tail = cur + len;
	char* buf = ctx->buf + tail;
	long blen = ctx->ptr - tail;
	long tlen = skip_right_visually(buf, blen, vtail);

	if(tlen > 0)
		ctx->ends = tail + tlen;
	else
		ctx->ends = tail;

	add_then_tail(ctx, cur, len, tlen);
}

/* The problem here is mostly deciding how to display the inserted text,
   and how the window should be scrolled if the text goes over the right
   border.

   Note the terminal cannot "insert", it only over-types characters,
   which becomes important when we're trying to insert something
   in the middle of the line. */

static void insert(CTX, char* inp, int len)
{
	char* buf = ctx->buf;
	int ptr = ctx->ptr;
	int cur = ctx->cur;
	int ends = ctx->ends;

	if(ptr + len >= ctx->max)
		return;

	if(cur < ptr)
		memmove(buf + cur + len, buf + cur, ptr - cur);

	memcpy(buf + cur, inp, len);

	int tlen = ends - cur;
	ctx->ptr = ptr + len;
	ctx->cur = cur + len;
	ctx->ends += len;

	int width = ctx->cols;
	int right = width - 2;

	int viswi = ctx->viswi;
	int vinsd = visual_width(buf + cur, len);
	int vtail = visual_width(buf + cur + len, tlen);
	int vhead = viswi - vtail;
	ctx->viswi += vinsd;

	if(vhead + vinsd > width - 2)
		scroll_right(ctx);
	else if(viswi + vinsd > right)
		add_trim_tail(ctx, cur, len, right - (vhead + vinsd));
	else if(vtail)
		add_then_tail(ctx, cur, len, tlen);
	else
		add_immediate(ctx, cur, len);

	set_scroll_window(ctx, ctx->show);
}

static void control_d(CTX)
{
	if(ctx->ptr > ctx->sep)
		return;

	exit(ctx, 0);
}

static void reset_input(CTX)
{
	ctx->cur = ctx->sep;
	ctx->ptr = ctx->sep;
	set_scroll_redraw(ctx, 0);
}

static void enter_cmd(CTX)
{
	char* cmd = ctx->buf + ctx->sep;
	int len = ctx->ptr - ctx->sep;

	/* Dump the whole input, but only if it does not fit on the screen;
	   otherwise it would erase then print the same stuff. */

	if(ctx->show || ctx->ends < ctx->ptr) {
		outcsi(ctx, 1, 0, 'G');
		outcsi(ctx, 0, 0, 'K');
		outbuf(ctx, 0, ctx->ptr);
	}

	ctx->hptr = ctx->buf + ctx->ptr;

	leave_term(ctx);

	parse(ctx, cmd, len);

	enter_term(ctx);

	ctx->hptr = ctx->buf + ctx->max;

	reset_input(ctx);
}

static int cursor_column(CTX)
{
	int show = ctx->show;
	int cur = ctx->cur;

	if(cur < show)
		return -1;

	return visual_width(ctx->buf + show, cur - show);
}

static void move_left(CTX)
{
	int sep = ctx->sep;
	int cur = ctx->cur;

	char* buf = ctx->buf + sep;
	long len = cur - sep;

	long skip = skip_left(buf, len);

	if(skip <= 0)
		return;
	if(skip > len)
		skip = len;

	ctx->cur = cur - skip;

	int col = cursor_column(ctx);

	if(col <= 2 && ctx->show > 0)
		scroll_left(ctx);
	else if(col > 0)
		outcsi(ctx, 0, 0, 'D');
}

static void move_right(CTX)
{
	int cur = ctx->cur;
	int ptr = ctx->ptr;

	char* buf = ctx->buf + cur;
	long len = ptr - cur;

	long skip = skip_right(buf, len);

	if(skip <= 0)
		return;
	if(skip > len)
		skip = len;

	ctx->cur = cur + skip;

	if(cursor_column(ctx) >= ctx->cols - 2)
		scroll_right(ctx);
	else
		outcsi(ctx, 0, 0, 'C');
}

static void back_realign_tail(CTX, int vstep)
{
	while(vstep-- > 0)
		outstr(ctx, "\x08");

	set_scroll_window(ctx, ctx->show);

	int ends = ctx->ends;
	int cur = ctx->cur;

	if(cur >= ends) {
		outstr(ctx, "\033[K");
	} else {
		outstr(ctx, "\033[s");
		outbuf(ctx, cur, ends);
		outstr(ctx, "\033[K\033[u");
	}
}

static void delete(CTX, int len)
{
	char* buf = ctx->buf;
	int sep = ctx->sep;
	int cur = ctx->cur;
	int ptr = ctx->ptr;

	int left = cur - sep;

	if(len <= 0)
		return;
	if(len > left)
		len = left;

	int oldc = cur;
	int newc = cur - len;

	if(oldc < ptr)
		memmove(buf + newc, buf + oldc, ptr - oldc);

	ctx->ptr = ptr - len;
	int old = cursor_column(ctx);
	ctx->cur = cur - len;
	int col = cursor_column(ctx);

	if(col <= 2 && ctx->show > 0)
		scroll_left(ctx);
	else if(col > 0)
		back_realign_tail(ctx, old - col);
}

static void backspace(CTX)
{
	char* buf = ctx->buf + ctx->sep;
	int len = ctx->cur - ctx->sep;

	delete(ctx, skip_left(buf, len));
}

static void control_u(CTX)
{
	delete(ctx, ctx->cur - ctx->sep);
}

static void control_w(CTX)
{
	char* buf = ctx->buf + ctx->sep;
	int len = ctx->cur - ctx->sep;

	delete(ctx, skip_left_until_space(buf, len));
}

static void control_a(CTX)
{
	ctx->cur = ctx->sep;
	set_scroll_redraw(ctx, 0);
}

static void control_e(CTX)
{
	long span = visual_width(ctx->buf, ctx->ptr);

	ctx->cur = ctx->ptr;

	if(span <= ctx->cols - 2)
		set_scroll_redraw(ctx, 0);
	else
		scroll_left(ctx);
}

static void control_k(CTX)
{
	ctx->ptr = ctx->cur;

	if(ctx->ends > ctx->ptr)
		ctx->ends = ctx->ptr;

	outcsi(ctx, 0, 0, 'K');
}

static void handle_ctrl(CTX, int c)
{
	switch(c) {
		case 0x01: return control_a(ctx);
		case 0x04: return control_d(ctx);
		case 0x05: return control_e(ctx);
		case 0x08: return backspace(ctx);
		case 0x0B: return control_k(ctx);
		case 0x0C: return redraw_flush(ctx);
		case 0x0D: return enter_cmd(ctx);
		case 0x15: return control_u(ctx);
		case 0x17: return control_w(ctx);
	}
}

static int try_escape_seq(CTX, char* buf, char* end)
{
	long len = end - buf;
	char* p;

	if(len < 2)
		return 0;
	if(buf[1] != '[')
		return 1; /* silently drop Escape */

	for(p = buf + 2; p < end; p++)
		if(*p >= '0' && *p <= '9')
			continue;
		else if(*p == ';')
			continue;
		else
			break;
	if(p >= end)
		return 0; /* incomplete sequence */

	if(*p == 'D')
		move_left(ctx);
	if(*p == 'C')
		move_right(ctx);

	return p - buf + 1;
}

static int try_multibyte(CTX, char* buf, char* end)
{
	long len = end - buf;
	int ret;

	if((ret = deutf(buf, len, NULL)) == 0)
		return ret;
	if(ret < 0)
		ret = -ret;

	insert(ctx, buf, ret);

	return ret;
}

int handle_input(CTX, char* buf, int len)
{
	char* ptr = buf;
	char* end = buf + len;
	int got;

	while(ptr < end) {
		byte c = (*ptr & 0xFF);

		if(c == 0x1B) { /* Escape */
			if((got = try_escape_seq(ctx, ptr, end)))
				ptr += got;
			else break;
		} else if(!(c & 0x80)) {
			if(c == 0x7F)
				backspace(ctx);
			else if(c < 0x20)
				handle_ctrl(ctx, c);
			else
				insert(ctx, ptr, 1);
			ptr++;
		} else { /* multi-byte utf8 sequence */
			if((got = try_multibyte(ctx, ptr, end)))
				ptr += got;
			else break;
		}

		flush(ctx);
	}

	redraw_flush(ctx);

	return ptr - buf;
}

/* Init and fini routines */

static void ttyioctl(char* tag, int req, void* ptr)
{
	int ret;

	if((ret = sys_ioctl(STDIN, req, ptr)) < 0)
		fail("ioctl", tag, ret);
}

static void enter_term(CTX)
{
	ttyioctl("TCSETS", TCSETS, &ctx->tsi);
}

static void leave_term(CTX)
{
	flush(ctx);
	ttyioctl("TCSETS", TCSETS, &ctx->tso);
	sys_write(STDOUT, "\n", 1);
}

void init_input(CTX)
{
	struct termios ts;
	struct winsize ws;

	ttyioctl("TCGETS", TCGETS, &ts);

	memcpy(&ctx->tso, &ts, sizeof(ts));
	ts.iflag &= ~(IGNPAR | ICRNL | IXON | IMAXBEL);
	ts.oflag &= ~(OPOST | ONLCR);
	ts.lflag &= ~(ICANON | ECHO);
	memcpy(&ctx->tsi, &ts, sizeof(ts));

	ttyioctl("TIOCGWINSZ", TIOCGWINSZ, &ws);
	ctx->cols = ws.col;

	enter_term(ctx);

	reset_input(ctx);
	redraw_flush(ctx);
}

void fini_input(CTX)
{
	leave_term(ctx);
}

void update_winsz(CTX)
{
	struct winsize ws;

	if(sys_ioctl(STDIN, TIOCGWINSZ, &ws) < 0)
		return;

	ctx->cols = ws.col;

	char* buf = ctx->buf;
	int cur = ctx->cur;
	int vcur = visual_width(buf, cur);

	outcsi(ctx, 0, 0, 'J'); /* erase spilled lines at the bottom */

	if(vcur < ws.col - 2) {
		set_scroll_redraw(ctx, 0);
		goto done;
	}

	int show = ctx->show;
	int vvis = visual_width(buf + show, cur - show);

	if(vvis < ws.col - 2) {
		set_scroll_redraw(ctx, show);
		goto done;
	}

	scroll_left(ctx);
done:
	redraw_flush(ctx);
}
