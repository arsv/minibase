#include <format.h>

#include "base.h"
#include "dump.h"
#include "rtnl/if.h"

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
