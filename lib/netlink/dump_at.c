#include <bits/ints.h>

#include <string.h>
#include <printf.h>

#include "base.h"
#include "dump.h"
#include "attr.h"

static void nl_dump_attr(char* pref, struct nlattr* at);

static void nl_dump_rec(char* pref, struct nlattr* base)
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
		nl_dump_attr(newpref, at);
}

static int nl_attr_is_printable_str(struct nlattr* at)
{
	if(!nl_attr_is_zstr(at))
		return 0;

	char* p;
	
	for(p = at->payload; *p; p++)
		if(*p < 0x20 || *p >= 0x7F)
			return 0;

	return 1;
}

static void nl_hexbytes(char* outbuf, int outlen, char* inbuf, int inlen)
{
	static const char digits[] = "0123456789ABCDEF";

	char* p = outbuf;
	int i;

	for(i = 0; i < inlen; i++) {
		char c = inbuf[i];
		*p++ = digits[(c >> 4) & 15];
		*p++ = digits[(c >> 0) & 15];
		*p++ = ' ';
	} if(i) p--;

	*p = '\0';
}

static void nl_hexstring(char* output, char* inbuf, int len)
{
	int i;

	for(i = 0; i < len; i++)
		if(inbuf[i] >= 0x20 && inbuf[i] <= 0x7F)
			output[i] = inbuf[i];
		else
			output[i] = '.';
	output[i] = '\0';
}

static void nl_dump_attr(char* pref, struct nlattr* at)
{
	char bytebuf[3*20];

	int len = at->len - sizeof(*at);
	char* buf = at->payload;
	
	if(len <= 16)
		nl_hexbytes(bytebuf, sizeof(bytebuf), buf, len);
	else
		bytebuf[0] = '\0';

	if(len == 0) {
		tracef("%s %i empty\n", pref, at->type);
	} else if(nl_attr_is_nest(at)) {
		tracef("%s %i: nest\n", pref, at->type);
		nl_dump_rec(pref, at);
	} else if(nl_attr_is_printable_str(at)) {
		tracef("%s %i: \"%s\"\n",
				pref, at->type, buf);
#if BITS == 64
	} else if(len == 8) {
		tracef("%s %i: %s = long %li\n",
				pref, at->type, bytebuf, *(int64_t*)buf);
#endif
	} else if(len == 4) {
		tracef("%s %i: %s = int %i\n",
				pref, at->type, bytebuf, *(int32_t*)buf);
	} else if(len == 2) {
		tracef("%s %i: %s = short %i\n",
				pref, at->type, bytebuf, *(int16_t*)buf);
	} else if(len < 15) {
		char prn[len+1];
		nl_hexstring(prn, buf, len);
		tracef("%s %i: %s    %s\n",
				pref, at->type, bytebuf, prn);
	} else {
		tracef("%s %i: %i bytes\n",
				pref, at->type, len);
		nl_hexdump(buf, len);
	}
}


void nl_dump_attrs_in(char* buf, int len)
{
	struct nlattr* at;

	for(at = nl_attr_0_in(buf, len); at; at = nl_attr_n_in(buf, len, at))
		nl_dump_attr(" ATTR", at);
}
