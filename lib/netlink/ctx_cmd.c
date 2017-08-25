#include <bits/errno.h>
#include <bits/types.h>
#include <bits/null.h>

#include "base.h"
#include "attr.h"
#include "ctx.h"

static void* nl_set_err(struct netlink* nl, int err)
{
	nl->err = -err;
	return NULL;
}

static void* nl_msg_err(struct netlink* nl, struct nlmsg* msg)
{
	struct nlerr* nle = (struct nlerr*) msg;

	if(msg->len < sizeof(*nle))
		nl->err = -EBADMSG;
	else
		nl->err = nle->errno;

	return NULL;
}

struct nlmsg* nl_recv_seq(struct netlink* nl)
{
	struct nlmsg* msg;

	while((msg = nl_recv(nl)))
		if(!msg->seq)
			continue;
		else if(msg->seq != nl->seq)
			return nl_set_err(nl, EBADMSG);
		else break;

	return msg;
}

struct nlmsg* nl_recv_expect(struct netlink* nl, int hdrsize, int one)
{
	struct nlmsg* msg = nl_recv_seq(nl);

	if(!msg)
		return NULL;
	if(msg->type == NLMSG_DONE)
		return nl_set_err(nl, one ? EBADMSG : 0);
	if(msg->type == NLMSG_ERROR)
		return nl_msg_err(nl, msg);
	if(msg->len < hdrsize)
		return nl_set_err(nl, EBADMSG);

	return msg;
}

struct nlmsg* nl_recv_reply(struct netlink* nl, int hdrsize)
{
	return nl_recv_expect(nl, hdrsize, EBADMSG);
}

struct nlmsg* nl_recv_multi(struct netlink* nl, int hdrsize)
{
	return nl_recv_expect(nl, hdrsize, 0);
}

struct nlmsg* nl_tx_msg(struct netlink* nl)
{
	if(nl->txend < sizeof(struct nlmsg))
		return NULL;
	return (struct nlmsg*)(nl->txbuf);
}

int nl_send_recv_ack(struct netlink* nl)
{
	struct nlmsg* msg;
	struct nlerr* nle;

	if(!(msg = nl_tx_msg(nl)))
		return -EINVAL;

	msg->flags |= NLM_F_ACK;

	if(nl_send(nl))
		return nl->err;

	msg = nl_recv_seq(nl);

	if(!(nle = nl_err(msg)))
		return (nl->err = -EBADMSG);

	return (nl->err = nle->errno);
}

int nl_send_dump(struct netlink* nl)
{
	struct nlmsg* msg;

	if(!(msg = nl_tx_msg(nl)))
		return -EINVAL;

	msg->flags |= NLM_F_DUMP;

	return nl_send(nl);
}
