#include <format.h>
#include <string.h>
#include <util.h>

#include <netlink.h>
#include <netlink/attr.h>
#include <netlink/dump.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/route.h>

static void hexdump(char* inbuf, int inlen)
{
	int spacelen = 4 + 3 + 3 + 2;
	int hexlen = 16*3;
	int charlen = 16;
	int total = spacelen + hexlen + charlen;
	static const char digits[] = "0123456789ABCDEF";

	char linebuf[total];
	char* e = linebuf + sizeof(linebuf) - 1;
	*e = '\n';

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
		writeall(STDERR, linebuf, e - linebuf + 1);
	}
}

static void nl_dump_msg_hdr(struct nlmsg* msg)
{
	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, "MSG");
	p = fmtstr(p, e, " len=");
	p = fmtint(p, e, msg->len);
	p = fmtstr(p, e, " type=");
	p = fmtint(p, e, msg->type);
	p = fmtstr(p, e, " flags=");
	p = fmthex(p, e, msg->flags);
	p = fmtstr(p, e, " seq=");
	p = fmtint(p, e, msg->seq);
	p = fmtstr(p, e, " pid=");
	p = fmtint(p, e, msg->pid);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
}

void nl_dump_msg(struct nlmsg* msg)
{
	int paylen = msg->len - sizeof(*msg);

	if(paylen >= 0)
		nl_dump_msg_hdr(msg);
	if(paylen > 0)
		hexdump(msg->payload, paylen);
}

void nl_dump_gen(struct nlgen* gen)
{
	nl_dump_msg_hdr(&gen->nlm);

	FMTBUF(p, e, buf, 50);
	p = fmtstr(p, e, " GENL");
	p = fmtstr(p, e, " cmd=");
	p = fmtint(p, e, gen->cmd);
	p = fmtstr(p, e, " version=");
	p = fmtint(p, e, gen->version);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	nl_dump_attrs_in(NLPAYLOAD(gen));
}

void nl_dump_err(struct nlerr* msg)
{
	struct nlmsg* nlm = &msg->nlm;

	FMTBUF(p, e, buf, 150);

	if(msg->errno)
		p = fmtstr(p, e, "ERR");
	else
		p = fmtstr(p, e, "ACK");

	p = fmtstr(p, e, " len=");
	p = fmtint(p, e, nlm->len);
	p = fmtstr(p, e, " type=");
	p = fmtint(p, e, nlm->type);
	p = fmtstr(p, e, " flags=");
	p = fmthex(p, e, nlm->flags);
	p = fmtstr(p, e, " seq=");
	p = fmtint(p, e, nlm->seq);
	p = fmtstr(p, e, " pid=");
	p = fmtint(p, e, nlm->pid);

	if(msg->errno) {
		p = fmtstr(p, e, " errno=");
		p = fmtint(p, e, msg->errno);
	}

	p = fmtstr(p, e, "\n  >");

	p = fmtstr(p, e, " len=");
	p = fmtint(p, e, msg->len);
	p = fmtstr(p, e, " type=");
	p = fmtint(p, e, msg->type);
	p = fmtstr(p, e, " flags=");
	p = fmthex(p, e, msg->flags);
	p = fmtstr(p, e, " seq=");
	p = fmtint(p, e, msg->seq);
	p = fmtstr(p, e, " pid=");
	p = fmtint(p, e, msg->pid);

	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);
}

void nl_dump_ifinfo(struct ifinfomsg* msg)
{
	nl_dump_msg_hdr(&msg->nlm);

	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, " IFINFO");
	p = fmtstr(p, e, " family=");
	p = fmtint(p, e, msg->family);
	p = fmtstr(p, e, " type=");
	p = fmtint(p, e, msg->type);
	p = fmtstr(p, e, " index=");
	p = fmtint(p, e, msg->index);
	p = fmtstr(p, e, " flags=");
	p = fmthex(p, e, msg->flags);
	p = fmtstr(p, e, " change=");
	p = fmthex(p, e, msg->change);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	nl_dump_attrs_in(NLPAYLOAD(msg));
}

void nl_dump_ifaddr(struct ifaddrmsg* msg)
{
	nl_dump_msg_hdr(&msg->nlm);

	FMTBUF(p, e, buf, 100);
	p = fmtstr(p, e, " IFADDR");
	p = fmtstr(p, e, " family=");
	p = fmtint(p, e, msg->family);
	p = fmtstr(p, e, " prefixlen=");
	p = fmtint(p, e, msg->prefixlen);
	p = fmtstr(p, e, " flags=");
	p = fmthex(p, e, msg->flags);
	p = fmtstr(p, e, " scope=");
	p = fmtint(p, e, msg->scope);
	p = fmtstr(p, e, " index=");
	p = fmtint(p, e, msg->index);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	nl_dump_attrs_in(NLPAYLOAD(msg));
}

void nl_dump_rtmsg(struct rtmsg* msg)
{
	nl_dump_msg_hdr(&msg->nlm);

	FMTBUF(p, e, buf, 200);
	p = fmtstr(p, e, " RTMSG");
	p = fmtstr(p, e, " family=");
	p = fmtint(p, e, msg->family);
	p = fmtstr(p, e, " dst_len=");
	p = fmtint(p, e, msg->dst_len);
	p = fmtstr(p, e, " src_len=");
	p = fmtint(p, e, msg->src_len);
	p = fmtstr(p, e, " tos=");
	p = fmtint(p, e, msg->tos);
	p = fmtstr(p, e, "\n");
	p = fmtstr(p, e, "      ");
	p = fmtstr(p, e, " table=");
	p = fmtint(p, e, msg->table);
	p = fmtstr(p, e, " protocol=");
	p = fmtint(p, e, msg->protocol);
	p = fmtstr(p, e, " scope=");
	p = fmtint(p, e, msg->scope);
	p = fmtstr(p, e, " type=");
	p = fmtint(p, e, msg->type);
	p = fmtstr(p, e, " flags=");
	p = fmthex(p, e, msg->flags);
	FMTENL(p, e);

	writeall(STDERR, buf, p - buf);

	nl_dump_attrs_in(NLPAYLOAD(msg));
}

#define trycast(msg, tt, ff) \
	if(msg->len >= sizeof(tt)) \
		return ff((tt*)msg); \
	else \
		break

void nl_dump_rtnl(struct nlmsg* msg)
{
	switch(msg->type) {
		case RTM_NEWLINK:
		case RTM_DELLINK:
		case RTM_GETLINK:
			trycast(msg, struct ifinfomsg, nl_dump_ifinfo);
		case RTM_NEWADDR:
		case RTM_DELADDR:
		case RTM_GETADDR:
			trycast(msg, struct ifaddrmsg, nl_dump_ifaddr);
		case RTM_NEWROUTE:
		case RTM_DELROUTE:
		case RTM_GETROUTE:
			trycast(msg, struct rtmsg, nl_dump_rtmsg);
	}

	nl_dump_msg(msg);
}

void nl_dump_genl(struct nlmsg* msg)
{
	struct nlerr* err;
	struct nlgen* gen;

	if((err = nl_err(msg)))
		nl_dump_err(err);
	else if((gen = nl_gen(msg)))
		nl_dump_gen(gen);
	else
		nl_dump_msg(msg);
}
