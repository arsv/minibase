#include <sys/socket.h>
#include <sys/creds.h>
#include <sys/file.h>
#include <sys/mman.h>

#include <netlink.h>
#include <netlink/recv.h>
#include <netlink/dump.h>
#include <netlink/rtnl/mgrp.h>
#include <util.h>
#include <main.h>

ERRTAG("rtevents");

char nlbuf[8*1024];

static int open_socket(void)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW;
	int protocol = NETLINK_ROUTE;

	int mgrp_link = RTMGRP_LINK | RTMGRP_NOTIFY;
	int mgrp_ipv4 = RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;

	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = mgrp_link | mgrp_ipv4
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
	if((ret = nr_recv(fd, &nr)) < 0)
		fail("recv", "NETLINK", ret);

	while((msg = nr_next(&nr)))
		nl_dump_rtnl(msg);

	goto recv;
}
