#include <nlusctl.h>
#include <format.h>
#include <string.h>
#include <output.h>
#include <util.h>

static int is_nest(struct ucattr* at)
{
	void* buf = uc_payload(at);
	int len = uc_paylen(at);

	void* p = buf;
	void* e = buf + len;

	while(p < e) {
		struct ucattr* at = p;
		int alen = at->len;
		int skip = (alen + 3) & ~3;

		if(!skip) break;

		p += skip;
	}

	return (p == e);
}

static char* fmt_chr(char* p, char* e, byte c)
{
	if(c < 20 || c >= 0x7F)
		p = fmtchar(p, e, '.');
	else
		p = fmtchar(p, e, c);

	return p;
}

static char* fmt_bytes(char* p, char* e, byte* data, int len)
{
	int i;

	p = fmtstr(p, e, "[ ");
	for(i = 0; i < len; i++) {
		p = fmtbyte(p, e, data[i]);
		p = fmtchar(p, e, ' ');
	}

	p = fmtstr(p, e, "]  ");

	for(i = 0; i < len; i++) {
		p = fmt_chr(p, e, data[i]);
	}

	return p;
}

static void prefix(struct bufout* bo, int lvl)
{
	for(int i = 0; i < lvl; i++)
		bufout(bo, "  ", 2);
}

static void hexline(struct bufout* bo, int lvl, byte* data, int len)
{
	int i;

	prefix(bo, lvl);

	FMTBUF(p, e, buf, 128);

	p = fmtstr(p, e, "# ");

	for(i = 0; i < 16; i++) {
		if(i < len)
			p = fmtbyte(p, e, data[i]);
		else
			p = fmtstr(p, e, "  ");

		p = fmtstr(p, e, " ");
	}

	p = fmtstr(p, e, "   ");

	for(i = 0; i < 16; i++) {
		if(i < len)
			p = fmt_chr(p, e, data[i]);
		else
			p = fmtstr(p, e, " ");
	}

	FMTENL(p, e);

	bufout(bo, buf, p - buf);
}

static void dump_data(struct bufout* bo, int lvl, struct ucattr* at)
{
	byte* data = uc_payload(at);
	int i, len = uc_paylen(at);
	int lines = len / 16;
	int trail = len % 16;
	int count = 0;

	for(i = 0; i < lines; i++) {
		hexline(bo, lvl, data + count, 16);
		count += 16;
	} if(trail > 0) {
		hexline(bo, lvl, data + count, trail);
	}
}

static void dump_attr(struct bufout* bo, int lvl, struct ucattr* at);

static void dump_nest(struct bufout* bo, int lvl, struct ucattr* at)
{
	struct ucattr* bt;

	for(bt = uc_get_0(at); bt; bt = uc_get_n(at, bt))
		dump_attr(bo, lvl, bt);
}

static void dump_attr(struct bufout* bo, int lvl, struct ucattr* at)
{
	int paylen = uc_paylen(at);
	void* payload = uc_payload(at);

	prefix(bo, lvl);

	FMTBUF(p, e, buf, 120);

	p = fmtstr(p, e, "ATTR ");
	p = fmtint(p, e, at->key);

	if(paylen <= 0) {
		p = fmtstr(p, e, " flag");
	} else if(paylen <= 8) {
		p = fmtstr(p, e, "  ");
		p = fmt_bytes(p, e, payload, paylen);
	} else {
		p = fmtstr(p, e, " +");
		p = fmtint(p, e, paylen);
	}

	FMTENL(p, e);

	bufout(bo, buf, p - buf);

	if(paylen <= 8)
		return;
	if(is_nest(at))
		dump_nest(bo, lvl + 1, at);
	else
		dump_data(bo, lvl + 1, at);
}

void uc_dump(struct ucattr* msg)
{
	byte output[2048];
	struct bufout bo;

	bufoutset(&bo, STDERR, output, sizeof(output));

	int code = uc_repcode(msg);
	int paylen = uc_paylen(msg);

	FMTBUF(p, e, buf, 80);

	p = fmtstr(p, e, "NLUS ");
	p = fmtint(p, e, code);

	if(paylen > 0) {
		p = fmtstr(p, e, " +");
		p = fmtuint(p, e, paylen);
	};

	FMTENL(p, e);

	bufout(&bo, buf, p - buf);

	if(is_nest(msg)) {
		struct ucattr* at;

		for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
			dump_attr(&bo, 1, at);

	} else {
		dump_data(&bo, 1, msg);
	}

	bufoutflush(&bo);
}
