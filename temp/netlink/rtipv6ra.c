#include <sys/socket.h>
#include <sys/creds.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <netlink.h>
#include <netlink/recv.h>
#include <netlink/dump.h>
#include <netlink/rtnl/mgrp.h>
#include <netlink/rtnl/ndusr.h>
#include <printf.h>
#include <string.h>
#include <format.h>
#include <endian.h>
#include <util.h>
#include <main.h>

ERRTAG("rtipv6ra");

#define RTMGRP_ND_USEROPT (1<<19)

char nlbuf[8*1024];

struct ndopt
{
	uint8_t type;
	uint8_t len;
} __attribute__((packed));

struct ipv6
{
	uint8_t addr[16];
};

struct ndopt_rdnss
{
	uint8_t type;
	uint8_t len;
	uint16_t _1;
	uint32_t time;
	struct ipv6 ips[];
} __attribute__((packed));

static char* fmt_ipv6(char* p, char* e, byte ip[16])
{
	int i;

	for(i = 0; i < 16; i++) {
		p = fmtbyte(p, e, ip[i]);

		if(i == 15)
			break;
		if(!(i % 2))
			continue;

		p = fmtchar(p, e, ':');
	}

	return p;
}

static void print_dns(struct ipv6* ip, int time)
{
	FMTBUF(p, e, buf, 200);
	p = fmt_ipv6(p, e, ip->addr);
	FMTEND(p, e);

	tracef("    DNS %s %us\n", buf, time);
}

static void dump_rdnss(struct ndopt_rdnss* nd)
{
	int len = nd->len; /* in units of 8 */

	if(len < 3)
		return;
	if(!(len & 1))
		return;

	int n = len >> 1;
	int time = ntohl(nd->time);

	for(int i = 0; i < n; i++)
		print_dns(&(nd->ips[i]), time);
}

static void dump_options(void* data, uint len)
{
	void* p = data;
	void* e = data + len;

	while(p < e) {
		struct ndopt* nd = p;

		if(p + sizeof(*nd) > e) break;

		int type = nd->type;
		uint len = nd->len << 3;

		p += len;

		if(p > e) break;

		tracef("  OPT %u len %u\n", type, len);

		if(type == 25)
			dump_rdnss((struct ndopt_rdnss*)nd);
	}

	if(p < e)
		warn("truncated option", NULL, 0);
}

static void dump_nl_useropt(struct nlmsg* msg)
{
	struct nduseroptmsg* ndu = (struct nduseroptmsg*)msg;

	tracef("RTNL message %i\n", msg->type);

	if(msg->type != RTM_NEWUSEROPT)
		return;

	tracef("  ND family=%u len=%u ifi=%u type=%u code=%u\n",
			ndu->family, ndu->optslen, ndu->ifindex,
			ndu->type, ndu->code);

	if(ndu->family != 10) /* AF_INET6 */
		return;
	if(ndu->type != 134) /* ND_ROUTER_ADVERT */
		return;
	if(ndu->code != 0)
		return;

	void* msg_end = (void*)msg + msg->len;
	void* opt_end = (void*)ndu->payload + ndu->optslen;

	if(opt_end > msg_end) {
		warn("message truncated", NULL, 0);
		return;
	}

	dump_options(ndu->payload, ndu->optslen);
}

#if 1

static int open_socket(void)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW;
	int protocol = NETLINK_ROUTE;

	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = RTMGRP_ND_USEROPT
	};
	int fd, ret;

	if((fd = sys_socket(domain, type, protocol)) < 0)
		fail("socket", "NETLINK", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		fail("bind", "NETLINK", ret);

	return fd;
}

int main(noargs)
{
	struct nrbuf nr;
	struct nlmsg* msg;
	int ret, fd;

	nr_buf_set(&nr, nlbuf, sizeof(nlbuf));

	fd = open_socket();
recv:
	if((ret = nr_recv(fd, &nr)) < 0) fail("recv", "NETLINK", ret);

	while((msg = nr_next(&nr)))
		dump_nl_useropt(msg);

	goto recv;
}

#else

static byte sample[] = {
	0x0A, 0x00, 0x18, 0x00, 0x03, 0x00, 0x00, 0x00,
	0x86, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x19, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1E,
	0xFD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01,
	0x14, 0x00, 0x01, 0x00, 0xFE, 0x80, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x4A, 0x8F, 0x5A, 0xFF,
	0xFE, 0xA8, 0x2D, 0xBF
};

int main(noargs)
{
	struct nlmsg* msg;
	byte buf[sizeof(*msg)+sizeof(sample)];

	msg = (struct nlmsg*)buf;
	memzero(msg, sizeof(*msg));
	msg->type = RTM_NEWUSEROPT;
	msg->len = sizeof(buf);

	memcpy(msg->payload, sample, sizeof(sample));

	nl_dump_rtnl(msg);

	dump_nl_useropt(msg);

	return 0;
};

#endif
