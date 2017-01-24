#include <bits/socket/netlink.h>

struct nlmsg;

struct netlink {
	int fd;
	int seq;
	int err;

	void* rxbuf;
	int rxlen;
	int rxend;

	void* txbuf;
	int txlen;
	int txend;
	int txover;

	struct nlmsg* rx;
	struct nlmsg* tx;

	int msgptr;
	int msgend;
};

void nl_init(struct netlink* nl);
void nl_set_txbuf(struct netlink* nl, void* buf, int len);
void nl_set_rxbuf(struct netlink* nl, void* buf, int len);
long nl_connect(struct netlink* nl, int protocol, int grps);
long nl_subscribe(struct netlink* nl, int id);

long nl_recv_chunk(struct netlink* nl);
long nl_send_txbuf(struct netlink* nl);

/* Synchronous (request-reply) queries */

struct nlmsg* nl_recv(struct netlink* nl);
struct nlmsg* nl_recv_seq(struct netlink* nl);
struct nlmsg* nl_recv_reply(struct netlink* nl, int hdrsize);
struct nlmsg* nl_recv_multi(struct netlink* nl, int hdrsize);

int nl_send(struct netlink* nl);
int nl_send_recv_ack(struct netlink* nl);
int nl_send_dump(struct netlink* nl);

struct nlgen* nl_send_recv_genl(struct netlink* nl);
struct nlgen* nl_recv_genl_nonseq(struct netlink* nl);
struct nlgen* nl_recv_genl_multi(struct netlink* nl);

#define nl_send_recv_reply(nl, vv) \
	(vv = nl_send(nl) ? NULL : (typeof(vv))nl_recv_reply(nl, sizeof(*vv)))

#define nl_recv_multi_into(nl, vv) \
	(vv = (typeof(vv))nl_recv_multi(nl, sizeof(*vv)))

/* WTF section */

int nl_ifindex(struct netlink* nl, const char* ifname);
