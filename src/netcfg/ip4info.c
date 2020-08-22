#include <bits/socket/inet.h>
#include <bits/errno.h>
#include <sys/creds.h>
#include <sys/socket.h>

#include <netlink.h>
#include <netlink/pack.h>
#include <netlink/recv.h>
#include <netlink/attr.h>
#include <netlink/rtnl/addr.h>
#include <netlink/rtnl/link.h>
#include <netlink/rtnl/route.h>

#include <string.h>
#include <output.h>
#include <format.h>
#include <heap.h>
#include <util.h>
#include <main.h>

/* Dumping ip stack state has almost nothing in common with configuring it,
   so this is not a part of ip4cfg, at least for now.

   Unlike ip4cfg which is expected to be used in startup scripts and such,
   this tool is only expected to be run manually, by a human user trying
   to figure out what's wrong with the configuration. So it should provide
   a nice overview of the configuration, even if it requires combining data
   from multiple RTNL requests. */

ERRTAG("ip4info");

/* Link and IP lists are cached in the heap, because we need freely
   accessible to format links and routes properly. Route data is not
   cached, just show one message at a time. */

char ifbuf[1024];

char txbuf[1024];
char rxbuf[5*1024];

char outbuf[1024];

struct top {
	int netlink;

	struct heap hp;
	struct bufout bo;
	struct ncbuf nc;
	struct nrbuf nr;

	struct ip4addr {
		uint ifi;
		uint flags;
		byte ip[4];
		byte mask;
	} **ips;

	struct ip4link {
		uint ifi;
		char name[20];
	} **ifs;
};

#define CTX struct top* ctx

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

static char* fmtipm(char* p, char* e, uint8_t* ip, uint8_t mask)
{
	p = fmtip(p, e, ip);
	p = fmtstr(p, e, "/");
	p = fmtint(p, e, mask);
	return p;
}

void** reindex(CTX, void* p0, void* p1, int size)
{
	if((p1 - p0) % size)
		fail("heap corrupted", NULL, 0);

	int n = (p1 - p0)/size;
	void** idx = halloc(&ctx->hp, (n+1)*sizeof(void*));

	int i;

	for(i = 0; i < n; i++)
		idx[i] = p0 + i*size;
	idx[i] = NULL;

	return idx;
}

typedef struct ip4link* linkptr;
typedef struct ip4addr* addrptr;

static void index_ifs(CTX, void* p0, void* p1)
{
	ctx->ifs = (linkptr*)reindex(ctx, p0, p1, sizeof(struct ip4link));
}

static void index_ips(CTX, void* p0, void* p1)
{
	ctx->ips = (addrptr*)reindex(ctx, p0, p1, sizeof(struct ip4addr));
}

static void fail_nlerr(struct nlmsg* msg, char* tag)
{
	struct nlerr* err;

	if(!(err = nl_err(msg)))
		fail("invalid netlink error", NULL, 0);
	else
		fail(NULL, tag, err->errno);
}

static void store_addr(CTX, struct ifaddrmsg* msg)
{
	uint8_t* addr = nl_bin(ifa_get(msg, IFA_ADDRESS), 4);

	if(!addr) return;

	struct ip4addr* rec = halloc(&ctx->hp, sizeof(*rec));

	rec->ifi = msg->index;
	rec->flags = msg->flags;
	rec->mask = msg->prefixlen;
	memcpy(rec->ip, addr, 4);
}

static void recv_all_addrs(CTX, char* cmdtag)
{
	struct ifaddrmsg* ifm;
	struct nrbuf* nr = &ctx->nr;
	int fd = ctx->netlink;
	int ret;
recv:
	if((ret = nr_recv(fd, nr)) <= 0)
		fail("recv", "NETLINK", ret);

	struct nlmsg* msg;
next:
	if(!(msg = nr_next(nr)))
		goto recv;
	if(msg->type == NLMSG_DONE)
		return;
	if(msg->type == NLMSG_ERROR)
		fail_nlerr(msg, cmdtag);
	if(msg->type != RTM_NEWADDR)
		fail("recv", "NETLINK", -EBADMSG);
	if(!(ifm = nl_cast(msg, sizeof(*ifm))))
		fail("recv", "NETLINK", -EBADMSG);

	store_addr(ctx, ifm);

	goto next;
}

static void* heap_ptr(CTX)
{
	return ctx->hp.ptr;
}

static void fetch_ips(CTX)
{
	struct ifaddrmsg* req;
	struct ncbuf* nc = &ctx->nc;
	int ret, fd = ctx->netlink;

	nc_header(nc, RTM_GETADDR, NLM_F_DUMP, 0);

	(void)nc_fixed(nc, sizeof(*req));

	if((ret = nc_send(fd, nc)) < 0)
		fail("send", "NETLINK", ret);

	void* p0 = heap_ptr(ctx);

	recv_all_addrs(ctx, "GETADDR");

	void* p1 = heap_ptr(ctx);

	index_ips(ctx, p0, p1);
}

static void store_iface(CTX, struct ifinfomsg* msg)
{
	char* name = nl_str(ifl_get(msg, IFLA_IFNAME));
	struct ip4link* rec = halloc(&ctx->hp, sizeof(*rec));

	rec->ifi = msg->index;
	memset(rec->name, 0, sizeof(rec->name));

	if(name && strlen(name) < sizeof(rec->name) - 1)
		memcpy(rec->name, name, strlen(name));
}

static void recv_all_ifaces(CTX, char* cmdtag)
{
	struct ifinfomsg* ifm;
	struct nrbuf* nr = &ctx->nr;
	int fd = ctx->netlink;
	int ret;
recv:
	if((ret = nr_recv(fd, nr)) <= 0)
		fail("recv", "NETLINK", ret);

	struct nlmsg* msg;
next:
	if(!(msg = nr_next(nr)))
		goto recv;
	if(msg->type == NLMSG_DONE)
		return;
	if(msg->type == NLMSG_ERROR)
		fail_nlerr(msg, cmdtag);
	if(msg->type != RTM_NEWLINK)
		fail("recv", "NETLINK", -EBADMSG);
	if(!(ifm = nl_cast(msg, sizeof(*ifm))))
		fail("recv", "NETLINK", -EBADMSG);

	store_iface(ctx, ifm);

	goto next;
}

static void fetch_ifs(CTX)
{
	struct ifinfomsg* req;
	struct ncbuf* nc = &ctx->nc;
	int ret, fd = ctx->netlink;

	nc_header(nc, RTM_GETLINK, NLM_F_DUMP, 0);

	(void)nc_fixed(nc, sizeof(*req));

	if((ret = nc_send(fd, nc)) < 0)
		fail("send", "NETLINK", ret);

	void* p0 = heap_ptr(ctx);

	recv_all_ifaces(ctx, "GETLINK");

	void* p1 = heap_ptr(ctx);

	index_ifs(ctx, p0, p1);
}

void show_iface(CTX, linkptr lk)
{
	char* p = ifbuf;
	char* e = ifbuf + sizeof(ifbuf) - 1;

	p = fmtstr(p, e, "  ");

	if(lk->name[0]) {
		p = fmtstr(p, e, lk->name);
	} else {
		p = fmtstr(p, e, "#");
		p = fmtint(p, e, lk->ifi);
	}

	p = fmtstr(p, e, ":");

	addrptr* qq;
	addrptr q;
	int hasaddr = 0;

	for(qq = ctx->ips; (q = *qq); qq++) {
		if(q->ifi != lk->ifi)
			continue;
		hasaddr = 1;
		p = fmtstr(p, e, " ");
		p = fmtipm(p, e, q->ip, q->mask);
	} if(!hasaddr) {
		p = fmtstr(p, e, " -");
	}

	*p++ = '\n';
	bufout(&ctx->bo, ifbuf, p - ifbuf);
}

static void banner(CTX, char* msg)
{
	bufout(&ctx->bo, msg, strlen(msg));
	bufout(&ctx->bo, "\n", 1);
}

static void list_ipconf(CTX)
{
	linkptr* ifs = ctx->ifs;
	linkptr* q;

	banner(ctx, "Interfaces");

	for(q = ifs; *q; q++) {
		show_iface(ctx, *q);
	}
}

char* fmt_route_dst(char* p, char* e, struct rtmsg* msg)
{
	uint8_t* ip = nl_bin(rt_get(msg, RTA_DST), 4);

	p = fmtstr(p, e, " ");

	if(msg->dst_len == 0)
		p = fmtstr(p, e, "*");
	else if(!ip)
		p = fmtstr(p, e, "x");
	else if(msg->dst_len == 32)
		p = fmtip(p, e, ip);
	else
		p = fmtipm(p, e, ip, msg->dst_len);

	return p;
}

static char* ifi_to_name(CTX, uint ifi)
{
	struct ip4link** q;

	for(q = ctx->ifs; *q; q++)
		if((*q)->ifi == ifi)
			break;

	return *q ? (*q)->name : NULL;
}

static char* fmt_route_dev(char* p, char* e, struct rtmsg* msg, CTX)
{
	uint32_t* oif;
	char* name;

	if(!(oif = nl_u32(rt_get(msg, RTA_OIF))))
		goto out;

	if((name = ifi_to_name(ctx, *oif))) {
		p = fmtstr(p, e, name);
	} else {
		p = fmtstr(p, e, "#");
		p = fmtint(p, e, *oif);
	}

	p = fmtstr(p, e, ":");
out:
	return p;
}

char* fmt_route_gw(char* p, char* e, struct rtmsg* msg)
{
	uint8_t* gw = nl_bin(rt_get(msg, RTA_GATEWAY), 4);

	if(gw) {
		p = fmtstr(p, e, " via ");
		p = fmtip(p, e, gw);
	}

	return p;
}

char* fmt_route_proto(char* p, char* e, struct rtmsg* msg)
{
	int proto = msg->protocol;

	if(proto == RTPROT_STATIC)
		p = fmtstr(p, e, "static");
	else if(proto == RTPROT_KERNEL)
		p = fmtstr(p, e, "kern");
	else if(proto == RTPROT_DHCP)
		p = fmtstr(p, e, "dhcp");
	else
		return p;

	p = fmtstr(p, e, " ");
	return p;
}

char* fmt_route_misc(char* p, char* e, struct rtmsg* msg)
{
	char* q = p;

	p = fmtstr(p, e, " (");

	p = fmt_route_proto(p, e, msg);

	p = p > q ? fmtstr(p-1, e, ")") : p - 1;

	return p;
}

static void show_route(CTX, struct rtmsg* msg)
{
	char* p = ifbuf;
	char* e = ifbuf + sizeof(ifbuf) - 1;

	if(msg->type == RTN_LOCAL || msg->type == RTN_BROADCAST)
		return;

	p = fmtstr(p, e, "  ");

	p = fmt_route_dev(p, e, msg, ctx);
	p = fmt_route_dst(p, e, msg);
	p = fmt_route_gw(p, e, msg);
	p = fmt_route_misc(p, e, msg);

	*p++ = '\n';

	bufout(&ctx->bo, ifbuf, p - ifbuf);
}

static void recv_all_routes(CTX, char* cmdtag)
{
	struct rtmsg* rtm;
	struct nrbuf* nr = &ctx->nr;
	int fd = ctx->netlink;
	int ret;
recv:
	if((ret = nr_recv(fd, nr)) <= 0)
		fail("recv", "NETLINK", ret);

	struct nlmsg* msg;
next:
	if(!(msg = nr_next(nr)))
		goto recv;
	if(msg->type == NLMSG_DONE)
		return;
	if(msg->type == NLMSG_ERROR)
		fail_nlerr(msg, cmdtag);
	if(msg->type != RTM_NEWROUTE)
		fail("recv", "NETLINK", -EBADMSG);
	if(!(rtm = nl_cast(msg, sizeof(*rtm))))
		fail("recv", "NETLINK", -EBADMSG);

	show_route(ctx, rtm);

	goto next;
}

static void list_routes(CTX)
{
	struct rtmsg* req;
	struct ncbuf* nc = &ctx->nc;
	int fd = ctx->netlink;
	int ret;

	nc_header(nc, RTM_GETROUTE, NLM_F_DUMP, 0);

	if(!(req = nc_fixed(nc, sizeof(*req))))
		goto send;

	req->family = AF_INET;
send:
	if((ret = nc_send(fd, nc)) < 0)
		fail("send", "NETLINK", ret);

	banner(ctx, "Routes");

	recv_all_routes(ctx, "GETROUTE");
}

static void setup(CTX)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW | SOCK_CLOEXEC;
	int protocol = NETLINK_ROUTE;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = 0
	};
	int fd, ret;

	hinit(&ctx->hp, PAGE);

	if((fd = sys_socket(domain, type, protocol)) < 0)
		fail("socket", "NETLINK", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		fail("bind", "NETLINK", ret);

	ctx->netlink = fd;

	bufoutset(&ctx->bo, STDOUT, outbuf, sizeof(outbuf));

	nc_buf_set(&ctx->nc, txbuf, sizeof(txbuf));
	nr_buf_set(&ctx->nr, rxbuf, sizeof(rxbuf));
}

static void flush(CTX)
{
	bufoutflush(&ctx->bo);
}

int main(int argc, char** argv)
{
	struct top context, *ctx = &context;
	(void)argv;
	int i = 1;

	if(i < argc)
		fail("too many arguments", NULL, 0);

	memzero(ctx, sizeof(*ctx));

	setup(ctx);

	fetch_ifs(ctx);
	fetch_ips(ctx);

	list_ipconf(ctx);
	list_routes(ctx);

	flush(ctx);

	return 0;
}
