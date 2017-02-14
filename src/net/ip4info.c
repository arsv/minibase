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

struct ip4link {
	unsigned ifi;
	char name[20];
};

char ifbuf[1024];

char txbuf[512];
char rxbuf[6*1024];

static struct nlattr* ifa_get(struct ifaddrmsg* msg, uint16_t key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static struct nlattr* ifl_get(struct ifinfomsg* msg, uint16_t key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static struct nlattr* rt_get(struct rtmsg* msg, uint16_t key)
{
	return nl_attr_k_in(NLPAYLOAD(msg), key);
}

static char* fmtip(char* p, char* e, uint8_t* ip)
{
	p = fmtint(p, e, ip[0]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[1]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[2]);
	p = fmtstr(p, e, ".");
	p = fmtint(p, e, ip[3]);
	return p;
}

static char* fmtipm(char* p, char* e, uint8_t* ip, uint8_t mask)
{
	p = fmtip(p, e, ip);
	p = fmtstr(p, e, "/");
	p = fmtint(p, e, mask);
	return p;
}

void** reindex(struct heap* hp, void* p0, void* p1, int size)
{
	if((p1 - p0) % size)
		fail("heap corrupted", NULL, 0);

	int n = (p1 - p0)/size;
	void** idx = halloc(hp, (n+1)*sizeof(void*));

	int i;

	for(i = 0; i < n; i++)
		idx[i] = p0 + i*size;
	idx[i] = NULL;

	return idx;
}

struct ip4link** index_ifs(struct heap* hp, void* p0, void* p1)
{
	return (struct ip4link**)reindex(hp, p0, p1, sizeof(struct ip4link));
}

struct ip4addr** index_ips(struct heap* hp, void* p0, void* p1)
{
	return (struct ip4addr**)reindex(hp, p0, p1, sizeof(struct ip4addr));
}

void store_ip(struct heap* hp, struct ifaddrmsg* msg)
{
	//char* name = nl_str(ifa_get(msg, IFA_LABEL));
	uint8_t* addr = nl_bin(ifa_get(msg, IFA_ADDRESS), 4);

	if(!addr) return;

	struct ip4addr* rec = halloc(hp, sizeof(*rec));

	rec->ifi = msg->index;
	rec->flags = msg->flags;
	rec->mask = msg->prefixlen;
	memcpy(rec->ip, addr, 4);
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
		store_ip(hp, msg);
	if(nl->err)
		fail("netlink", "RTM_GETADDR", nl->err);

	void* p1 = hp->ptr;

	return index_ips(hp, p0, p1);
}

void store_if(struct heap* hp, struct ifinfomsg* msg)
{
	char* name = nl_str(ifl_get(msg, IFLA_IFNAME));
	struct ip4link* rec = halloc(hp, sizeof(*rec));

	rec->ifi = msg->index;
	memset(rec->name, 0, sizeof(rec->name));

	if(name && strlen(name) < sizeof(rec->name) - 1)
		memcpy(rec->name, name, strlen(name));
}

struct ip4link** fetch_ifs(struct netlink* nl, struct heap* hp)
{
	struct ifinfomsg* msg;

	nl_header(nl, msg, RTM_GETLINK, 0,
		.family = 0,
		.type = 0,
		.index = 0,
		.flags = 0,
		.change = 0);

	if(nl_send_dump(nl))
		fail("netlink", "RTM_GETLINK", nl->err);

	void* p0 = hp->ptr;

	while((nl_recv_multi_into(nl, msg)))
		store_if(hp, msg);
	if(nl->err)
		fail("netlink", "RTM_GETLINK", nl->err);

	void* p1 = hp->ptr;

	return index_ifs(hp, p0, p1);
}

void show_iface(struct ip4link* ifs, struct ip4addr** ips)
{
	char* p = ifbuf;
	char* e = ifbuf + sizeof(ifbuf) - 1;

	p = fmtstr(p, e, "link#");
	p = fmtint(p, e, ifs->ifi);

	if(*(ifs->name)) {
		p = fmtstr(p, e, " ");
		p = fmtstr(p, e, ifs->name);
	}

	p = fmtstr(p, e, ":");

	struct ip4addr **qq, *q;
	int hasaddr = 0;

	for(qq = ips; (q = *qq); qq++) {
		if(q->ifi != ifs->ifi)
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

void list_ipconf(struct ip4link** ifs, struct ip4addr** ips)
{
	struct ip4link** q;

	for(q = ifs; *q; q++)
		show_iface(*q, ips);
}

char* fmt_route_dst(char* p, char* e, struct rtmsg* msg)
{
	uint8_t* ip = nl_bin(rt_get(msg, RTA_DST), 4);

	if(msg->dst_len == 0) {
		p = fmtstr(p, e, "default");
	} else if(!ip) {
		p = fmtstr(p, e, "no-dst");
	} else if(msg->dst_len == 32) {
		p = fmtip(p, e, ip);
	} else {
		p = fmtipm(p, e, ip, msg->dst_len);
	}
	p = fmtstr(p, e, " ");

	return p;
}

char* ifi_to_name(struct ip4link** ifs, int ifi)
{
	struct ip4link** q;

	for(q = ifs; *q; q++)
		if((*q)->ifi == ifi)
			break;
	
	return *q ? (*q)->name : NULL;
}

char* fmt_route_dev(char* p, char* e, struct rtmsg* msg, struct ip4link** ifs)
{
	uint32_t* oif;
	char* name;

	if(!(oif = nl_u32(rt_get(msg, RTA_OIF))))
		goto out;

	p = fmtstr(p, e, "to ");

	if((name = ifi_to_name(ifs, *oif))) {
		p = fmtstr(p, e, name);
	} else {
		p = fmtstr(p, e, "#");
		p = fmtint(p, e, *oif);
	}

	p = fmtstr(p, e, " ");
out:	
	return p;
}

char* fmt_route_gw(char* p, char* e, struct rtmsg* msg)
{
	uint8_t* gw = nl_bin(rt_get(msg, RTA_GATEWAY), 4);

	if(gw) {
		p = fmtstr(p, e, "gw ");
		p = fmtip(p, e, gw);
		p = fmtstr(p, e, " ");
	}

	return p;
}

char* fmt_route_proto(char* p, char* e, struct rtmsg* msg)
{
	int proto = msg->protocol;

	if(proto == RTPROT_STATIC)
		p = fmtstr(p, e, "static");
	else if(proto == RTPROT_DHCP)
		p = fmtstr(p, e, "dhcp");
	else
		return p;

	p = fmtstr(p, e, " ");
	return p;
}

void show_route(struct rtmsg* msg, struct ip4link** ifs)
{
	char* p = ifbuf;
	char* e = ifbuf + sizeof(ifbuf) - 1;

	if(msg->type == RTN_LOCAL || msg->type == RTN_BROADCAST)
		return;

	p = fmtstr(p, e, "route ");

	p = fmt_route_dst(p, e, msg);
	p = fmt_route_dev(p, e, msg, ifs);
	p = fmt_route_gw(p, e, msg);
	p = fmt_route_proto(p, e, msg);

	*p++ = '\n';
	writeout(ifbuf, p - ifbuf);
}

void list_routes(struct netlink* nl, struct ip4link** ifs)
{
	struct rtmsg* msg;

	nl_header(nl, msg, RTM_GETROUTE, 0,
		.family = AF_INET);

	if(nl_send_dump(nl))
		fail("netlink", "RTM_GETROUTE", nl->err);

	writeout("\n", 1);

	while((nl_recv_multi_into(nl, msg)))
		show_route(msg, ifs);
	if(nl->err)
		fail("netlink", "RTM_GETROUTE", nl->err);
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

	struct ip4link** ifs = fetch_ifs(&nl, &hp);
	struct ip4addr** ips = fetch_ips(&nl, &hp);

	list_ipconf(ifs, ips);
	list_routes(&nl, ifs);

	flushout();

	return 0;
}
