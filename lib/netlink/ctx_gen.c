#include <bits/errno.h>
#include <bits/types.h>
#include <null.h>

#include "base.h"
#include "attr.h"
#include "ctx.h"

static void* nl_set_err(struct netlink* nl, int err)
{
	nl->err = -err;
	return NULL;
}

struct nlgen* nl_send_recv_genl(struct netlink* nl)
{
	if(nl_send(nl)) return NULL;

	struct nlmsg* msg = nl_recv_seq(nl);

	if(!msg) return NULL;

	if(msg->len < sizeof(struct nlgen)) return NULL;

	struct nlgen* gen = nl_gen(msg);

	if(!gen) return nl_set_err(nl, EBADMSG);

	return gen;
}

struct nlgen* nl_recv_genl(struct netlink* nl)
{
	struct nlmsg* msg = nl_recv(nl);
	struct nlgen* gen;

	if(msg->seq)	
		return nl_set_err(nl, EBADMSG);

	if(!(gen = nl_gen(msg)))
		return nl_set_err(nl, EBADMSG);

	return gen;
}

struct nlgen* nl_recv_genl_multi(struct netlink* nl)
{
	struct nlmsg* msg = nl_recv_multi(nl, sizeof(struct nlgen));

	if(!msg) return NULL;

	struct nlgen* gen = nl_gen(msg);

	if(!msg) return nl_set_err(nl, EBADMSG);

	return gen;
}
