#include <sys/ioctl.h>

#include <format.h>
#include <string.h>
#include <util.h>
#include "passblk.h"

/* Draw input box, process user input, flash red border in case passphrase
   in incorrect. This is only ever expected to run on Linux console, so all
   code typically available there can be used.

   Depending on configuration, this tool *may* happen to run during KMS init,
   which can change console rows/cols, so we have to handle SIGWINCH. */

#define CSI "\033["

static void output(char* s, char* p)
{
	writeall(STDOUT, s, p - s);
}

static void apply_termio_flags(CTX)
{
	int ret;
	struct termios ts;

	if((ret = sys_ioctl(0, TCGETS, &ts)) < 0)
		fail("ioctl", "TCGETS", ret);

	memcpy(&ctx->tio, &ts, sizeof(ts));
	ts.iflag |= IUTF8;
	ts.iflag &= ~(IGNPAR | ICRNL | IXON | IMAXBEL);
	ts.oflag &= ~(OPOST | ONLCR);
	ts.lflag &= ~(ICANON | ECHO);

	if((ret = sys_ioctl(0, TCSETS, &ts)) < 0)
		fail("ioctl", "TCSETS", ret);

	ctx->termactive = 1;
}

static void reset_termio_flags(CTX)
{
	struct termios* tio = &ctx->tio;
	int ret;

	if(!ctx->termactive)
		return;
	if((ret = sys_ioctl(0, TCSETS, tio)) < 0)
		fail("ioctl", "TCSETS", ret);
}

static void update_box_position(CTX)
{
	int rows = ctx->rows;
	int cols = ctx->cols;

	int inlen = ((cols >= 60) ? 40 : (cols - 20));

	ctx->inlen = inlen;
	ctx->incol = cols/2 - inlen/2;
	ctx->inrow = rows/2;
}

static void update_term_size(CTX)
{
	int ret;
	struct winsize ws;

	if((ret = sys_ioctl(0, TIOCGWINSZ, &ws)) < 0)
		quit(ctx, "ioctl", "TIOCGWINSZ", ret);
	if(ws.row <= 0 || ws.col <= 0)
		quit(ctx, "terminal does not report window size", NULL, 0);
	if(ws.row < 10 || ws.col < 40)
		quit(ctx, "terminal window is too small", NULL, 0);

	ctx->cols = ws.col;
	ctx->rows = ws.row;

	update_box_position(ctx);
}

static char* fmt_move(char* p, char* e, int r, int c)
{
	p = fmtstr(p, e, CSI);
	p = fmtint(p, e, r);
	p = fmtchar(p, e, ';');
	p = fmtint(p, e, c);
	p = fmtchar(p, e, 'H');
	return p;
}

static char* fmt_repeat(char* p, char* e, char* str, int n)
{
	int i;

	for(i = 0; i < n; i++)
		p = fmtstr(p, e, str);

	return p;
}

static char* fmt_clear_screen(char* p, char* e)
{
	p = fmt_move(p, e, 1, 1);
	p = fmtstr(p, e, CSI "J");
	return p;
}

static char* fmt_park_cursor(char* p, char* e, CTX)
{
	return fmt_move(p, e, ctx->rows, 1);
}

static char* fmt_box_color(char* p, char* e, CTX)
{
	int state = ctx->state;

	if(state == ST_INVALID)
		return fmtstr(p, e, CSI "0;1;31m");
	if(state == ST_HASHING)
		return fmtstr(p, e, CSI "0;1;30m");

	return fmtstr(p, e, CSI "0;1;37m");
}

static char* fmt_inp_color(char* p, char* e, CTX)
{
	int state = ctx->state;

	if(state == ST_HASHING)
		return fmtstr(p, e, CSI "0;1;30m");
	if(state == ST_INVALID)
		return fmtstr(p, e, CSI "0;31m");

	return fmtstr(p, e, CSI "0;37m");
}

static char* fmt_color_reset(char* p, char* e)
{
	return fmtstr(p, e, CSI "0m");
}

static char* fmt_box_top(char* p, char* e, CTX, int r, int c, int w)
{
	p = fmt_move(p, e, r, c);
	p = fmtstr(p, e, "┏");
	p = fmt_repeat(p, e, "━", w);
	p = fmtstr(p, e, "┓");

	return p;
}

static char* fmt_box_pad(char* p, char* e, CTX, int r, int c, int w)
{
	p = fmt_move(p, e, r, c);
	p = fmtstr(p, e, "┃");
	p = fmt_repeat(p, e, " ", w);
	p = fmtstr(p, e, "┃");

	return p;
}

static char* fmt_box_bot(char* p, char* e, CTX, int r, int c, int w)
{
	p = fmt_move(p, e, r, c);
	p = fmtstr(p, e, "┗");
	p = fmt_repeat(p, e, "━", w);
	p = fmtstr(p, e, "┛");

	return p;
}

static char* fmt_box_input(char* p, char* e, CTX)
{
	int inlen = ctx->inlen;
	int psptr = ctx->psptr;
	char* abc = ctx->pass + psptr;
	int abclen = ctx->plen - psptr;
	int padlen = inlen - abclen;

	if(abclen > inlen) abclen = inlen;

	p = fmt_move(p, e, ctx->inrow, ctx->incol);

	if(ctx->showpass)
		p = fmtstrn(p, e, abc, abclen);
	else
		p = fmt_repeat(p, e, "*", abclen);

	p = fmt_repeat(p, e, "_", padlen);

	return p;
}

static char* fmt_draw_dialog(char* p, char* e, CTX)
{
	int c = ctx->incol - 3;
	int r = ctx->inrow - 2;
	int w = ctx->inlen + 4;

	p = fmt_box_color(p, e, ctx);
	p = fmt_box_top(p, e, ctx, r++, c, w);
	p = fmt_box_pad(p, e, ctx, r++, c, w);
	p = fmt_box_pad(p, e, ctx, r++, c, w);
	p = fmt_box_pad(p, e, ctx, r++, c, w);
	p = fmt_box_bot(p, e, ctx, r++, c, w);
	p = fmt_inp_color(p, e, ctx);
	p = fmt_box_input(p, e, ctx);
	p = fmt_color_reset(p, e);

	return p;
}

static void redraw_dialog(CTX, int state)
{
	ctx->state = state;

	FMTBUF(p, e, buf, 1024);
	p = fmt_draw_dialog(p, e, ctx);
	p = fmt_park_cursor(p, e, ctx);
	FMTEND(p, e);

	output(buf, p);
}

static void redraw_input(CTX)
{
	FMTBUF(p, e, buf, 512);
	p = fmt_inp_color(p, e, ctx);
	p = fmt_box_input(p, e, ctx);
	p = fmt_color_reset(p, e);
	p = fmt_park_cursor(p, e, ctx);
	FMTEND(p, e);

	output(buf, p);
}

static void refresh_screen(CTX)
{
	FMTBUF(p, e, buf, 1024);

	p = fmt_draw_dialog(p, e, ctx);
	p = fmt_park_cursor(p, e, ctx);

	FMTEND(p, e);

	output(buf, p);
}

static void clear_input_box(CTX)
{
	int r = ctx->inrow - 3;
	int c = ctx->incol - 5;
	int w = ctx->inlen + 10;

	FMTBUF(p, e, buf, 1024);

	for(int i = 0; i < 7; i++) {
		p = fmt_move(p, e, r++, c);
		p = fmt_repeat(p, e, " ", w);
	}

	p = fmt_park_cursor(p, e, ctx);

	FMTEND(p, e);

	output(buf, p);
}

void start_terminal(CTX)
{
	apply_termio_flags(ctx);
	update_term_size(ctx);
	clear_input_box(ctx);
	refresh_screen(ctx);
}

static void show_add_char(CTX, char c)
{
	int inrow = ctx->inrow;
	int incol = ctx->incol;
	int psptr = ctx->psptr;
	int plen = ctx->plen;
	int coff = plen - psptr;

	if(!ctx->showpass)
		c = '*';

	FMTBUF(p, e, buf, 100);
	p = fmt_move(p, e, inrow, incol + coff - 1);
	p = fmt_inp_color(p, e, ctx);
	p = fmtchar(p, e, c);
	p = fmt_color_reset(p, e);
	p = fmt_park_cursor(p, e, ctx);
	FMTEND(p, e);

	output(buf, p);
}

static void show_del_char(CTX)
{
	int inrow = ctx->inrow;
	int incol = ctx->incol;
	int psptr = ctx->psptr;
	int plen = ctx->plen;
	int coff = plen - psptr;

	FMTBUF(p, e, buf, 100);
	p = fmt_move(p, e, inrow, incol + coff);
	p = fmt_inp_color(p, e, ctx);
	p = fmtchar(p, e, '_');
	p = fmt_color_reset(p, e);
	p = fmt_park_cursor(p, e, ctx);
	FMTEND(p, e);

	output(buf, p);
}

static int adjust_input_ptr(CTX)
{
	int inlen = ctx->inlen;
	int psptr = ctx->psptr;
	int plen = ctx->plen;
	int right = plen - psptr;

	if(right >= inlen) {
		ctx->psptr = psptr + 10;
		return !0;
	} else if(psptr > 0 && plen < inlen) {
		ctx->psptr = 0;
		return !0;
	} else if(psptr > 0 && right < inlen/2) {
		ctx->psptr = psptr < 10 ? 0 : psptr - 10;
		return !0;
	}

	return 0;
}

static void add_char(CTX, char c)
{
	int i = ctx->plen;

	if(i >= sizeof(ctx->pass))
		return;

	ctx->pass[i] = c;
	ctx->plen = i + 1;

	if(adjust_input_ptr(ctx))
		redraw_input(ctx);
	else
		show_add_char(ctx, c);
}

static void del_char(CTX)
{
	int i = ctx->plen;

	if(i <= 0)
		return;

	ctx->plen = i - 1;

	if(adjust_input_ptr(ctx))
		redraw_input(ctx);
	else
		show_del_char(ctx);
}

static void set_timer(CTX, int sec)
{
	ctx->needwait = 1;
	ctx->ts.sec = sec;
	ctx->ts.nsec = 0;
}

static void terminate_input(CTX)
{
	clear_input_box(ctx);
	reset_termio_flags(ctx);
}

static void enter_pass(CTX)
{
	if(!ctx->plen)
		return;

	ctx->showpass = 0;

	redraw_dialog(ctx, ST_HASHING);

	if(!unwrap_keydata(ctx)) { /* success */
		terminate_input(ctx);
		decrypt_parts(ctx);
		_exit(0x00);
	} else { /* failure, bad passphrase or something */
		memzero(ctx->pass, sizeof(ctx->pass));
		redraw_dialog(ctx, ST_INVALID);
		set_timer(ctx, 1);
	}
}

static void toggle_showpass(CTX)
{
	ctx->showpass = !ctx->showpass;

	redraw_input(ctx);
}

static void clear_input(CTX)
{
	ctx->plen = 0;
	ctx->psptr = 0;

	redraw_input(ctx);
}

static void reset_input(CTX)
{
	ctx->plen = 0;
	ctx->psptr = 0;

	redraw_dialog(ctx, ST_INPUT);
}

static void handle_reg_key(CTX, byte c)
{
	if(ctx->state != ST_INPUT)
		return;

	if(c >= 0x20 && c < 0x7F)
		add_char(ctx, c);
	else if(c == 0x7F)
		del_char(ctx);
	else if(c == 0x0C) /* C-l */
		refresh_screen(ctx);
	else if(c == 0x0D) /* Enter */
		enter_pass(ctx);
	else if(c == 0x08) /* C-h */
		del_char(ctx);
	else if(c == 0x09) /* Tab */
		toggle_showpass(ctx);
	else if(c == 0x15) /* C-u */
		clear_input(ctx);
}

static void handle_inchr(CTX, byte c)
{
	int keyst = ctx->keyst;

	if(keyst == IS_REG) {
		if(c == 0x1B)
			ctx->keyst = IS_ESC;
		else
			handle_reg_key(ctx, c);
	} else if(keyst == IS_ESC) {
		if(c == '[')
			ctx->keyst = IS_CSI;
		else
			ctx->keyst = IS_REG;
	} else if(keyst == IS_CSI) {
		if(c >= '0' && c <= '9')
			return;
		if(c == ';')
			return;

		ctx->keyst = IS_REG;
	} else {
		ctx->keyst = IS_REG;
	}
}

void handle_input(CTX, char* buf, int len)
{
	char* p = buf;
	char* e = p + len;

	if(ctx->state == ST_INVALID)
		reset_input(ctx);

	while(p < e) handle_inchr(ctx, *p++);
}

void handle_user_end(CTX)
{
	terminate_input(ctx);
	_exit(0xFF);
}

void handle_sigwinch(CTX)
{
	update_term_size(ctx);

	FMTBUF(p, e, buf, 1024);
	p = fmt_clear_screen(p, e);
	p = fmt_draw_dialog(p, e, ctx);
	p = fmt_park_cursor(p, e, ctx);
	FMTEND(p, e);

	output(buf, p);
}

void handle_timeout(CTX)
{
	int state = ctx->state;

	if(state != ST_INVALID)
		return;

	reset_input(ctx);
}

void quit(CTX, const char* msg, char* arg, int err)
{
	terminate_input(ctx);
	fail(msg, arg, err);
}
