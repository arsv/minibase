#include <bits/null.h>
#include <sys/creds.h>
#include <sys/socket.h>

#include <string.h>

#include "ctx.h"
#include "base.h"

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

long nl_connect(struct netlink* nl, int protocol, int groups)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = groups
	};
	long ret;

	if((ret = sys_socket(domain, type, protocol)) < 0)
		return ret;

	nl->fd = ret;

	if((ret = sys_bind(nl->fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		return ret;

	return 0;
}

/* Bare send/recv working in terms of blocks in buffers */

static long nl_recv_chunk(struct netlink* nl, int flags)
{
	int fd = nl->fd;
	char* buf = nl->rxbuf + nl->rxend;
	int len = nl->rxlen - nl->rxend;
	long rd;

	if(len <= 0)
		return -ENOBUFS;

	if((rd = sys_recv(fd, buf, len, flags)) > 0) {
		nl->rxend += rd;
		nl->err = 0;
	} else {
		nl->err = rd;
	}

	return rd;
}

long nl_send_txbuf(struct netlink* nl)
{
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = 0,
		.groups = 0
	};

	long rd = sys_sendto(nl->fd, nl->txbuf, nl->txend, 0, &nls, sizeof(nls));

	nl->err = (rd < 0 ? rd : 0);

	return rd;
}

/* Message-oriented recv and send */

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

static int nl_no_rx_space(struct netlink* nl)
{
	if(nl->rxend + sizeof(struct nlmsg) < nl->rxlen)
		return 0;

	nl->err = -ENOMEM;
	return 1;
}

void nl_shift_rxbuf(struct netlink* nl)
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
	if(nl_no_rx_space(nl))
		return NULL;

	long rd;
	while((rd = nl_recv_chunk(nl, 0)) > 0) {
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

long nl_recv_nowait(struct netlink* nl)
{
	return nl_recv_chunk(nl, MSG_DONTWAIT);
}

struct nlmsg* nl_get_nowait(struct netlink* nl)
{
	int len;

	if((len = nl_got_message(nl)) <= 0)
		return NULL;

	nl->msgptr = nl->msgend;
	nl->msgend = nl->msgptr + len;

	return (struct nlmsg*)(nl->rxbuf + nl->msgptr);
}

int nl_send(struct netlink* nl)
{
	if(!nl->txend)
		return (nl->err = -EINVAL);
	if(nl->txover)
		return (nl->err = -ENOMEM);

	return (nl_send_txbuf(nl) <= 0);
}
