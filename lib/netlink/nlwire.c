#include <sys/socket.h>
#include <sys/bind.h>
#include <sys/recv.h>
#include <sys/getpid.h>
#include <sys/sendto.h>

#include <string.h>
#include <format.h>
#include <netlink.h>

void nl_init(struct netlink* nl)
{
	memset(nl, 0, sizeof(*nl));
}

void nl_set_txbuf(struct netlink* nl, void* buf, int len)
{
	nl->txbuf = buf;
	nl->txlen = len;
}

void nl_set_rxbuf(struct netlink* nl, void* buf, int len)
{
	nl->rxbuf = buf;
	nl->rxlen = len;
}

long nl_connect(struct netlink* nl, int protocol)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW;
	struct sockaddr_nl nls = {
		.nl_family = AF_NETLINK,
		.nl_pid = sysgetpid(),
		.nl_groups = 0
	};
	long ret;

	nls.nl_family = AF_NETLINK;

	if((ret = syssocket(domain, type, protocol)) < 0)
		return ret;

	nl->fd = ret;

	if((ret = sysbind(nl->fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		return ret;

	return 0;
}

static int nl_got_message(struct netlink* nl)
{
	int off = nl->msgend;
	int len = nl->rxend - off;

	if(len < sizeof(struct nlmsg))
		return 0;

	struct nlmsg* msg = (struct nlmsg*)(nl->rxbuf + off);

	if(msg->len > len)
		return 0;

	return msg->len;
}

/* Bare send/recv working in terms of blocks in buffers */

long nl_recv_chunk(struct netlink* nl)
{
	int remaining = nl->rxlen - nl->rxend;

	long rd = sysrecv(nl->fd, nl->rxbuf + nl->rxend, remaining, 0);

	if(rd > 0)
		nl->rxend += rd;

	nl->err = (rd >= 0 ? 0 : rd);

	return rd;
}

long nl_send_chunk(struct netlink* nl)
{
	struct sockaddr_nl nls = {
		.nl_family = AF_NETLINK,
		.nl_pid = 0,
		.nl_groups = 0
	};

	long rd = syssendto(nl->fd, nl->txbuf, nl->txlen, 0, &nls, sizeof(nls));

	nl->err = (rd < 0 ? rd : 0);

	return rd;
}

/* Message-oriented recv and send */

static int nl_no_rx_space(struct netlink* nl)
{
	if(nl->rxend + sizeof(struct nlmsg) < nl->rxlen)
		return 0;

	nl->err = -ENOMEM;
	return 1;
}

static void nl_shift_rxbuf(struct netlink* nl)
{
	int off = nl->msgend;

	char* p = nl->rxbuf;
	char* q = nl->rxbuf + off;
	int rem = nl->rxend - off;
	int i;

	for(i = 0; i < rem; i++)
		*p++ = *q++;

	nl->rxend = rem;
	nl->msgend = 0;
	nl->msgptr = 0;
}

struct nlmsg* nl_recv(struct netlink* nl)
{
	int len;

	if((len = nl_got_message(nl)) > 0)
		goto gotmsg;
	if(nl->msgend > 0)
		nl_shift_rxbuf(nl);
	else if(nl_no_rx_space(nl))
		return NULL;

	long rd;
	while((rd = nl_recv_chunk(nl)) > 0) {
		if((len = nl_got_message(nl)) > 0)
			goto gotmsg;
		if(nl_no_rx_space(nl))
			return NULL;
	}
	/* recv error or EOF; nl->err is set already */
	return NULL;

gotmsg:
	nl->msgptr = nl->msgend;
	nl->msgend = nl->msgptr + len;

	return (struct nlmsg*)(nl->rxbuf + nl->msgptr);
}

struct nlmsg* nl_recv_seq(struct netlink* nl)
{
	struct nlmsg* msg;

	while((msg = nl_recv(nl)))
		if(msg->seq == nl->seq)
			return msg;

	return NULL;
}

int nl_send(struct netlink* nl, int flags)
{
	if(!nl->txend)
		return (nl->err = -EINVAL);
	if(nl->txover)
		return (nl->err = -ENOMEM);

	struct nlmsg* msg = (struct nlmsg*)(nl->txbuf);

	msg->len = nl->txend;
	msg->flags = flags;

	return (nl_send_chunk(nl) <= 0);
}

struct nlmsg* nl_send_recv(struct netlink* nl, int flags)
{
	if(nl_send(nl, flags) < 0)
		return NULL;
	return nl_recv_seq(nl);
}

int nl_done(struct nlmsg* msg)
{
	return (msg->type == NLMSG_DONE);
}

int nl_inseq(struct netlink* nl, struct nlmsg* msg)
{
	return (msg->seq == nl->seq);
}
