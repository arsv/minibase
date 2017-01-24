#include <format.h>

#include "base.h"
#include "dump.h"
#include "attr.h"
#include "rtnl/addr.h"
#include "rtnl/link.h"
#include "rtnl/route.h"

static void nl_dump_msg_hdr(struct nlmsg* msg)
{
	eprintf("MSG len=%i type=%i flags=%X seq=%i pid=%i\n",
		msg->len, msg->type, msg->flags, msg->seq, msg->pid);
}

void nl_dump_msg(struct nlmsg* msg)
{
	nl_dump_msg_hdr(msg);

	int paylen = msg->len - sizeof(*msg);

	if(paylen > 0)
		nl_hexdump(msg->payload, paylen);
}

void nl_dump_gen(struct nlgen* gen)
{
	nl_dump_msg_hdr(&gen->nlm);

	eprintf(" GENL cmd=%i version=%i\n", gen->cmd, gen->version);

	nl_dump_attrs_in(NLPAYLOAD(gen));
}

void nl_dump_err(struct nlerr* msg)
{
	struct nlmsg* nlm = &msg->nlm;

	if(msg->errno)
		eprintf("ERR len=%i type=%i flags=%X seq=%i pid=%i errno=%i\n",
			nlm->len, nlm->type, nlm->flags, nlm->seq, nlm->pid,
			msg->errno);
	else
		eprintf("ACK len=%i type=%i flags=%X seq=%i pid=%i\n",
			nlm->len, nlm->type, nlm->flags, nlm->seq, nlm->pid);

	eprintf("  > len=%i type=%i flags=%X seq=%i pid=%i\n",
		msg->len, msg->type, msg->flags, msg->seq, msg->pid);
}

void nl_dump_ifaddr(struct ifaddrmsg* msg)
{
	nl_dump_msg_hdr(&msg->nlm);

	eprintf("    ifaddr family=%i prefix=%i flags=%i scope=%i index=%i\n",
			msg->family, msg->prefixlen, msg->flags, msg->scope,
			msg->index);

	nl_dump_attrs_in(msg->payload, msg->nlm.len - sizeof(*msg));
}

void nl_dump_rtmsg(struct rtmsg* msg)
{
	nl_dump_msg_hdr(&msg->nlm);

	eprintf(" RTMSG family=%i dst_len=%i src_len=%i tos=%i\n",
		msg->family, msg->dst_len, msg->src_len, msg->tos);
	eprintf("       table=%i protocol=%i scope=%i type=%i flags=%X\n",
		msg->table, msg->protocol, msg->scope, msg->type, msg->flags);

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
		case RTM_NEWADDR:
			trycast(msg, struct ifaddrmsg, nl_dump_ifaddr);
		case RTM_NEWROUTE:
			trycast(msg, struct rtmsg, nl_dump_rtmsg);
		default:
			nl_dump_msg(msg);
	}
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
