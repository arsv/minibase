#include <format.h>
#include <string.h>
#include <util.h>
#include <nlusctl.h>

static void dump_attr(char* pref, struct ucattr* at);

static void dump_rec(char* pref, struct ucattr* bt)
{
	struct ucattr* at;

	FMTBUF(p, e, npf, strlen(pref) + 10);
	p = fmtstr(p, e, "  ");
	p = fmtstr(p, e, pref);
	FMTEND(p);

	for(at = uc_sub_0(bt); at; at = uc_sub_n(bt, at))
		dump_attr(npf, at);
}

static char* tag(char* p, char* e, char* pref, struct ucattr* at, char* type)
{
	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, "ATTR ");
	p = fmtint(p, e, at->key);
	p = fmtstr(p, e, type);
	return p;
}

static void output(char* buf, char* p)
{
	FMTENL(p);
	writeall(STDERR, buf, p - buf);
}

void at_empty(char* pref, struct ucattr* at)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " empty");
	output(buf, p);
}

void at_nest(char* pref, struct ucattr* at)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " nest");
	output(buf, p);

	dump_rec(pref, at);
}

void at_string(char* pref, struct ucattr* at, char* str)
{
	FMTBUF(p, e, buf, 30 + strlen(str));
	p = tag(p, e, pref, at, " ");
	p = fmtstr(p, e, "\"");
	p = fmtstr(p, e, str);
	p = fmtstr(p, e, "\"");
	output(buf, p);
}

void at_int(char* pref, struct ucattr* at, int* val)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " int ");
	p = fmtint(p, e, *val);
	output(buf, p);
}

void at_raw(char* pref, struct ucattr* at, void* data, int dlen)
{
	char* q = data;
	int i;

	FMTBUF(p, e, buf, 80);
	p = tag(p, e, pref, at, " raw");

	for(i = 0; i < dlen; i++) {
		p = fmtstr(p, e, " ");
		p = fmtbyte(p, e, q[i]);
	}

	output(buf, p);
}

void at_trash(char* pref, struct ucattr* at, void* data, int dlen)
{
	char* q = data;
	int i;

	FMTBUF(p, e, buf, 80);
	p = tag(p, e, pref, at, "");

	for(i = 0; i < dlen; i++) {
		if(i % 16 == 0) {
			output(buf, p);
			p = buf;
			p = fmtstr(p, e, pref);
			p = fmtstr(p, e, " ");
		}
		p = fmtstr(p, e, " ");
		p = fmtbyte(p, e, q[i]);
	}

	output(buf, p);
}

static void dump_attr(char* pref, struct ucattr* at)
{
	int paylen = uc_paylen(at);
	void* payload = uc_payload(at);
	int key = at->key;

	char* str;
	int* val;

	if(paylen == 0)
		at_empty(pref, at);
	else if((str = uc_is_str(at, key)))
		at_string(pref, at, str);
	else if(uc_is_nest(at, key))
		at_nest(pref, at);
	else if((val = uc_is_int(at, key)))
		at_int(pref, at, val);
	else if(paylen < 15)
		at_raw(pref, at, payload, paylen);
	else
		at_trash(pref, at, payload, paylen);
}


void dump_attrs_in(struct ucmsg* msg)
{
	struct ucattr* at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		dump_attr("  ", at);
}

static int printable(int c)
{
	return (c > 0x20 && c < 0x7F);
}

static void dump_header(struct ucmsg* msg)
{
	int cmd = msg->cmd;
	int paylen = msg->len - sizeof(*msg);

	FMTBUF(p, e, buf, 80);

	p = fmtstr(p, e, "NLUS");

	if(cmd < 0) {
		p = fmtstr(p, e, " error ");
		p = fmtint(p, e, -cmd);
	} else {
		int c1 = (cmd >> 24) & 0xFF;
		int c2 = (cmd >> 16) & 0xFF;

		if(printable(c1) && printable(c2)) {
			p = fmtstr(p, e, " ");
			p = fmtchar(p, e, c1);
			p = fmtchar(p, e, c2);
			p = fmtstr(p, e, "(");
			p = fmtint(p, e, cmd & 0xFFFF);
			p = fmtstr(p, e, ")");
		} else {
			p = fmtstr(p, e, " cmd=");
			p = fmtint(p, e, cmd);
		}
	}

	if(paylen > 0) {
		p = fmtstr(p, e, " payload ");
		p = fmtint(p, e, paylen);
	};

	FMTENL(p);

	writeall(STDERR, buf, p - buf);
}

void uc_dump(struct ucmsg* msg)
{
	dump_header(msg);
	dump_attrs_in(msg);
}
