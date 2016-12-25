#include <sys/socket.h>
#include <sys/bind.h>
#include <sys/recv.h>
#include <sys/getpid.h>
#include <sys/sendto.h>

#include <string.h>
#include <format.h>
#include <netlink.h>

void nl_hexdump(char* inbuf, int inlen)
{
	int spacelen = 4 + 3 + 3 + 2;
	int hexlen = 16*3;
	int charlen = 16;
	int total = spacelen + hexlen + charlen;
	static const char digits[] = "0123456789ABCDEF";

	char linebuf[total];
	char* e = linebuf + sizeof(linebuf) - 1;
	*e = '\0';

	int lines = (inlen/16) + (inlen & 15 ? 1 : 0);

	int l, c, i;
	char* p;

	for(l = 0; l < lines; l++) {
		memset(linebuf, ' ', e - linebuf);
		for(c = 0; c < 16; c++) {
			i = l*16 + c;
			if(i >= inlen) break;

			uint8_t x = inbuf[i];

			p = linebuf + 4 + 3*c + (c >= 8 ? 2 : 0);
			p[0] = digits[((x >> 4) & 15)];
			p[1] = digits[((x >> 0) & 15)];
		}

		for(c = 0; c < 16; c++) {
			i = l*16 + c;
			if(i >= inlen) break;

			uint8_t x = inbuf[i];

			p = linebuf + 4 + 16*3 + 4 + c;
			*p = (x >= 0x20 && x < 0x7F) ? x : '.';
		}
		eprintf("%s\n", linebuf);
	}
}

static void nl_dump_attr(char* pref, struct nlattr* attr);

static void nl_dump_rec(char* pref, struct nlattr* attr)
{
	int len = strlen(pref);
	char newpref[len + 3];

	newpref[0] = ' ';
	newpref[1] = '|';
	memcpy(newpref + 2, pref, len);
	newpref[len + 2] = '\0';

	struct nlattr* sub;

	for(sub = nl_sub_0(attr); sub; sub = nl_sub_n(attr, sub))
		nl_dump_attr(newpref, sub);
}

static int nl_is_printable_str(struct nlattr* at)
{
	if(!nl_is_str(at))
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

static void nl_dump_attr(char* pref, struct nlattr* attr)
{
	char bytebuf[3*20];

	int len = attr->len - sizeof(*attr);
	char* buf = attr->payload;
	
	if(len <= 16)
		nl_hexbytes(bytebuf, sizeof(bytebuf), buf, len);
	else
		bytebuf[0] = '\0';

	if(nl_is_nest(attr)) {
		eprintf("%s %i: nest\n", pref, attr->type);
		nl_dump_rec(pref, attr);
	} else if(nl_is_printable_str(attr)) {
		eprintf("%s %i: \"%s\"\n",
				pref, attr->type, buf);
	} else if(len == 8) {
		eprintf("%s %i: %s = long %li\n",
				pref, attr->type, bytebuf, *(int64_t*)buf);
	} else if(len == 4) {
		eprintf("%s %i: %s = int %i\n",
				pref, attr->type, bytebuf, *(int32_t*)buf);
	} else if(len == 2) {
		eprintf("%s %i: %s = short %i\n",
				pref, attr->type, bytebuf, *(int16_t*)buf);
	} else if(len <= 16) {
		eprintf("%s %i: %s\n",
				pref, attr->type, bytebuf);
	} else {
		eprintf("%s %i: %i bytes\n",
				pref, attr->type, len);
		nl_hexdump(buf, len);
	}
}

static void nl_dump_msg_hdr(struct nlmsg* msg)
{
	eprintf("MSG len=%i type=%i flags=%X seq=%i pid=%i\n",
		msg->len, msg->type, msg->flags, msg->seq, msg->pid);
}

static void nl_dump_gen_hdr(struct nlgen* msg)
{
	eprintf("GEN len=%i type=%i flags=%X seq=%i pid=%i cmd=%i ver=%i\n",
		msg->len, msg->type, msg->flags, msg->seq, msg->pid,
		msg->cmd, msg->version);
}

static void nl_dump_err_hdr(struct nlerr* msg)
{
	if(msg->errno)
		eprintf("ERR len=%i type=%i flags=%X seq=%i pid=%i errno=%i\n",
			msg->len, msg->type, msg->flags, msg->seq, msg->pid,
			msg->errno);
	else
		eprintf("ACK len=%i type=%i flags=%X seq=%i pid=%i\n",
			msg->len, msg->type, msg->flags, msg->seq, msg->pid);
}

static void nl_dump_raw(struct nlmsg* msg)
{
	int paylen = msg->len - sizeof(*msg);
	char* payload = msg->payload;

	nl_dump_msg_hdr(msg);

	if(paylen > 0)
		nl_hexdump(payload, paylen);
}

static void nl_dump_gen(struct nlgen* msg)
{
	struct nlattr* at;

	nl_dump_gen_hdr(msg);

	for(at = nl_get_0(msg); at; at = nl_get_n(msg, at))
		nl_dump_attr(" ATTR", at);
}


void nl_dump_attrs_in(char* buf, int len)
{
	struct nlattr* at;

	for(at = attr_0_in(buf, len); at; at = attr_n_in(buf, len, at))
		nl_dump_attr(" ATTR", at);
}

static void nl_dump_err(struct nlerr* msg)
{
	nl_dump_err_hdr(msg);

	char* payload = msg->payload;
	int paylen = msg->len - sizeof(*msg);

	struct nlmsg* orig = (struct nlmsg*)(payload);
	struct nlgen* ogen;

	if(paylen == orig->len) {
		if((ogen = nl_gen(orig)))
			nl_dump_gen(ogen);
		else
			nl_dump_raw(orig);
	} else if(paylen == sizeof(struct nlgen)) {
		nl_dump_gen_hdr((struct nlgen*)orig);
	} else if(paylen == sizeof(struct nlmsg)) {
		nl_dump_msg_hdr(orig);
	} else {
		nl_hexdump(payload, paylen);
	}
}

static void nl_dump(struct nlmsg* msg)
{
	struct nlgen* gen;
	struct nlerr* err;

	eprintf("------------------------------------------------------------\n");

	if((err = nl_err(msg)))
		nl_dump_err(err);
	else if((gen = nl_gen(msg)))
		nl_dump_gen(gen);
	else
		nl_dump_raw(msg);
}

void nl_dump_tx(struct netlink* nl)
{
	struct nlmsg* msg = (struct nlmsg*) nl->txbuf;

	if(!nl->txend)
		eprintf("TX buffer empty\n");
	else if(!msg->len)
		eprintf("TX message incomplete\n");
	else
		nl_dump(msg);
}

void nl_dump_rx(struct netlink* nl)
{
	struct nlmsg* msg = (struct nlmsg*)(nl->rxbuf + nl->msgptr);

	if(nl->msgend <= nl->msgptr)
		eprintf("RX buffer empty\n");
	else if((msg->len < sizeof(*msg))
	     || msg->len != nl->msgend - nl->msgptr)
		eprintf("RX message incomplete\n");
	else
		nl_dump(msg);
}
