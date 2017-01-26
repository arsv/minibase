#include <bits/socket/inet.h>
#include <bits/errno.h>

#include <netlink.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/route.h>
#include <netlink/dump.h>

#include <string.h>
#include <output.h>
#include <format.h>
#include <heap.h>
#include <util.h>
#include <fail.h>

#include "utils.h"

/* Dumping ip stack state has almost nothing in common with configuring it,
   so this is not a part of ip4cfg, at least for now.

   Unlike ip4cfg which is expected to be used in startup scripts and such,
   this tool is only expected to be run manually, by a human user trying
   to figure out what's wrong with the configuration. So it should provide
   a nice overview of the configuration, even if it requires combining data
   from multiple RTNL requests. */

ERRTAG = "ip4info";
ERRLIST = {
	REPORT(EPERM), REPORT(EBUSY), REPORT(ENOENT), REPORT(EBADF),
	REPORT(ENETDOWN), REPORT(EEXIST), REPORT(EOPNOTSUPP),
	REPORT(ECONNREFUSED), REPORT(EFAULT), REPORT(EINTR), REPORT(EINVAL),
	REPORT(ENOMEM), REPORT(ENOTCONN), REPORT(ENOTSOCK), REPORT(EBADMSG),
	REPORT(EAFNOSUPPORT), REPORT(ENOBUFS), REPORT(EPROTONOSUPPORT),
	RESTASNUMBERS
};

struct ip4addr {
	unsigned ifi;
	unsigned flags;
	uint8_t ip[4];
	uint8_t mask;
};

char ifbuf[1024];

char txbuf[1024];
char rxbuf[6*1024];

static struct nlattr* ifa_get(struct ifaddrmsg* msg, uint16_t key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static struct nlattr* ifl_get(struct ifinfomsg* msg, uint16_t key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static char* fmtipm(char* p, char* e, uint8_t* ip, uint8_t mask)
{
	p = fmtint(p, e, ip[0]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[1]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[2]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[3]);
	p = fmtstr(p, e, "/");
	p = fmtint(p, e, mask);
	return p;
}

void note_ip(struct heap* hp, struct ifaddrmsg* msg)
{
	char* name = nl_str(ifa_get(msg, IFA_LABEL));
	uint8_t* addr = nl_bin(ifa_get(msg, IFA_ADDRESS), 4);

	if(!name || !addr) return;

	struct ip4addr* rec = halloc(hp, sizeof(*rec));

	rec->ifi = msg->index;
	rec->flags = msg->flags;
	rec->mask = msg->prefixlen;
	memcpy(rec->ip, addr, 4);
}

struct ip4addr** index_ips(struct heap* hp, void* p0, void* p1)
{
	struct ip4addr* a0 = p0;
	struct ip4addr* a1 = p1;

	int n = a1 - a0;
	struct ip4addr** idx = halloc(hp, (n+1)*sizeof(struct ip4addr*));

	int i;

	for(i = 0; i < n; i++)
		idx[i] = a0 + i;
	idx[i] = NULL;

	return idx;
}

struct ip4addr** fetch_ips(struct netlink* nl, struct heap* hp)
{
	struct ifaddrmsg* msg;

	nl_header(nl, msg, RTM_GETADDR, 0,
		.family = AF_INET,
		.prefixlen = 0,
		.flags = 0,
		.scope = 0,
		.index = 0);

	if(nl_send_dump(nl))
		fail("netlink", "RTM_GETADDR", nl->err);

	void* p0 = hp->ptr;

	while((nl_recv_multi_into(nl, msg)))
		note_ip(hp, msg);
	if(nl->err)
		fail("netlink", "RTM_GETADDR", nl->err);

	void* p1 = hp->end;

	return index_ips(hp, p0, p1);
}

void show_iface(struct ifinfomsg* msg, struct ip4addr** ips)
{
	char* p = ifbuf;
	char* e = ifbuf + sizeof(ifbuf) - 1;

	char* name = nl_str(ifl_get(msg, IFLA_IFNAME));

	if(name)
		p = fmtstr(p, e, name);
	else
		p = fmtint(fmtstr(p, e, "#"), e, msg->index);
	p = fmtstr(p, e, ":");

	struct ip4addr **qq, *q;
	int hasaddr = 0;

	for(qq = ips; (q = *qq); qq++) {
		if(q->ifi != msg->index)
			continue;
		hasaddr = 1;
		p = fmtstr(p, e, " ");
		p = fmtipm(p, e, q->ip, q->mask);
	} if(!hasaddr) {
		p = fmtstr(p, e, " none");
	}

	*p++ = '\n';
	writeout(ifbuf, p - ifbuf);
}

void list_ipconf(struct netlink* nl, struct heap* hp)
{
	struct ifinfomsg* msg;

	struct ip4addr** ips = fetch_ips(nl, hp);

	nl_header(nl, msg, RTM_GETLINK, 0,
		.family = 0,
		.type = 0,
		.index = 0,
		.flags = 0,
		.change = 0);

	if(nl_send_dump(nl))
		fail("netlink", "RTM_GETADDR", nl->err);

	while((nl_recv_multi_into(nl, msg)))
		show_iface(msg, ips);
	if(nl->err)
		fail("netlink", "RTM_GETADDR", nl->err);
}

int main(int argc, char** argv)
{
	struct netlink nl;
	struct heap hp;

	int i = 1;

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));
	nl_connect(&nl, NETLINK_ROUTE, 0);

	hinit(&hp, PAGE);

	if(i < argc)
		fail("too many arguments", NULL, 0);

	list_ipconf(&nl, &hp);

	flushout();

	return 0;
}
