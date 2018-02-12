#include <bits/types.h>

#include <string.h>
#include <format.h>
#include <util.h>

#include "base.h"
#include "dump.h"
#include "attr.h"

static void dump_attr(char* pref, struct nlattr* at);

static void dump_rec(char* pref, struct nlattr* base)
{
	int plen = strlen(pref);
	char newpref[plen + 3];

	newpref[0] = ' ';
	newpref[1] = '|';
	memcpy(newpref + 2, pref, plen);
	newpref[plen + 2] = '\0';

	struct nlattr* at;
	char* buf = base->payload;
	int len = base->len - sizeof(*base);

	for(at = nl_attr_0_in(buf, len); at; at = nl_attr_n_in(buf, len, at))
		dump_attr(newpref, at);
}

static char* tag(char* p, char* e, char* pref, struct nlattr* at, char* type)
{
	p = fmtstr(p, e, pref);
	p = fmtstr(p, e, "ATTR ");
	p = fmtint(p, e, at->type);
	p = fmtstr(p, e, type);
	return p;
}

static void output(char* p, char* e, char* buf)
{
	FMTENL(p, e);
	writeall(STDERR, buf, p - buf);
}

static void at_empty(char* pref, struct nlattr* at)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " empty");
	output(p, e, buf);
}

static void at_nest(char* pref, struct nlattr* at)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " nest");
	output(p, e, buf);

	dump_rec(pref, at);
}

static void at_string(char* pref, struct nlattr* at, char* str)
{
	FMTBUF(p, e, buf, 30 + strlen(str));
	p = tag(p, e, pref, at, " str ");
	p = fmtstr(p, e, "\"");
	p = fmtstr(p, e, str);
	p = fmtstr(p, e, "\"");
	output(p, e, buf);
}

static void at_short(char* pref, struct nlattr* at, short* val)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " int ");
	p = fmtint(p, e, *val);
	output(p, e, buf);
}

static void at_int(char* pref, struct nlattr* at, int* val)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " int ");
	p = fmtint(p, e, *val);
	p = fmtstr(p, e, " (0x");
	p = fmthex(p, e, *val);
	p = fmtstr(p, e, ")");
	output(p, e, buf);
}

static void at_u64(char* pref, struct nlattr* at, uint64_t* val)
{
	FMTBUF(p, e, buf, 50);
	p = tag(p, e, pref, at, " int ");
	p = fmtu64(p, e, *val);
	output(p, e, buf);
}

static void at_hexshort(char* pref, struct nlattr* at)
{
	char* q = nl_payload(at);
	int dlen = nl_paylen(at);

	FMTBUF(p, e, buf, 80);
	p = tag(p, e, pref, at, " raw");

	for(int i = 0; i < dlen; i++) {
		p = fmtstr(p, e, " ");
		p = fmtbyte(p, e, q[i]);
	}

	output(p, e, buf);
}

static void at_hexdump(char* pref, struct nlattr* at)
{
	char* q = nl_payload(at);
	int dlen = nl_paylen(at);

	FMTBUF(p, e, buf, 80);
	p = tag(p, e, pref, at, "");

	for(int i = 0; i < dlen; i++) {
		if(i % 16 == 0) {
			output(p, e, buf);
			p = buf;
			p = fmtstr(p, e, pref);
			p = fmtstr(p, e, " ");
		}
		p = fmtstr(p, e, " ");
		p = fmtbyte(p, e, q[i]);
	}

	output(p, e, buf);
}

static char* printable_str(struct nlattr* at)
{
	byte* buf = nl_payload(at);
	byte* end = buf + nl_paylen(at);
	byte* p;

	for(p = buf; p < end - 1; p++)
		if(!*p)
			break;
		else if(*p < 0x20 || *p >= 0x7F)
			return NULL;

	if(*p || end - p > 4) /* improperly terminated */
		return NULL;

	return (char*)buf;
}

static void dump_attr(char* pref, struct nlattr* at)
{
	int paylen = nl_paylen(at);

	char* str;

	if(paylen == 0)
		at_empty(pref, at);
	else if(nl_attr_is_nest(at))
		at_nest(pref, at);
	else if((str = printable_str(at)))
		at_string(pref, at, str);
	else if(paylen == 8)
		at_u64(pref, at, nl_payload(at));
	else if(paylen == 4)
		at_int(pref, at, nl_payload(at));
	else if(paylen == 2)
		at_short(pref, at, nl_payload(at));
	else if(paylen <= 16)
		at_hexshort(pref, at);
	else
		at_hexdump(pref, at);
}


void nl_dump_attrs_in(char* buf, int len)
{
	struct nlattr* at;

	for(at = nl_attr_0_in(buf, len); at; at = nl_attr_n_in(buf, len, at))
		dump_attr("  ", at);
}
