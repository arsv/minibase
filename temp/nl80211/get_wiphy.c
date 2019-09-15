#include <bits/socket/inet.h>
#include <bits/errno.h>
#include <sys/socket.h>
#include <sys/creds.h>

#include <netlink.h>
#include <netlink/recv.h>
#include <netlink/pack.h>
#include <netlink/attr.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/nl80211.h>
#include <netlink/dump.h>

#include <string.h>
#include <output.h>
#include <format.h>
#include <heap.h>
#include <util.h>
#include <main.h>

ERRTAG("get_wiphy");

char txbuf[128];
char rxbuf[8*1024-256];

struct nrbuf nr;
struct ncbuf nc;

static int open_socket(void)
{
	int domain = PF_NETLINK;
	int type = SOCK_RAW;
	int protocol = NETLINK_GENERIC;
	struct sockaddr_nl nls = {
		.family = AF_NETLINK,
		.pid = sys_getpid(),
		.groups = 0
	};
	int fd, ret;

	if((fd = sys_socket(domain, type, protocol)) < 0)
		fail("socket", "NETLINK", fd);
	if((ret = sys_bind(fd, (struct sockaddr*)&nls, sizeof(nls))) < 0)
		fail("bind", "NETLINK", ret);

	return fd;
}

static struct nlgen* send_recv(int fd)
{
	int ret;
	struct nlmsg* msg;
	struct nlerr* err;
	struct nlgen* gen;

	if((ret = nc_send(fd, &nc)) < 0)
		fail("send", NULL, ret);
	if((ret = nr_recv(fd, &nr)) < 0)
		fail("recv", NULL, ret);

	if(!(msg = nr_next(&nr)))
		fail("recv", NULL, -EAGAIN);

	if((err = nl_err(msg))) {
		if((ret = err->errno) < 0)
			fail(NULL, NULL, ret);
		else
			fail(NULL, NULL, -EBADMSG);
	} else if(!(gen = nl_gen(msg))) {
		fail(NULL, NULL, -EBADMSG);
	}

	return gen;
}

static int query_group(int fd, char* name)
{
	nc_header(&nc, GENL_ID_CTRL, 0, 0);
	nc_gencmd(&nc, CTRL_CMD_GETFAMILY, 1);
	nc_put_str(&nc, CTRL_ATTR_FAMILY_NAME, name);

	struct nlgen* msg = send_recv(fd);

	uint16_t* grpid = nl_get_u16(msg, CTRL_ATTR_FAMILY_ID);

	if(!grpid)
		fail(NULL, NULL, -ENOENT);

	return *grpid;
}

int main(int argc, char** argv)
{
	int ifi;
	char* p;

	if(argc != 2)
		fail("bad call", NULL, 0);

	if(!(p = parseint(argv[1], &ifi)) || *p)
		fail("bad ifindex:", argv[1], 0);

	nc_buf_set(&nc, txbuf, sizeof(txbuf));
	nr_buf_set(&nr, rxbuf, sizeof(rxbuf));

	int fd = open_socket();
	int nl80211 = query_group(fd, "nl80211");

	nc_header(&nc, nl80211, 0, 0);
	nc_gencmd(&nc, NL80211_CMD_GET_WIPHY, 0);
	nc_put_int(&nc, NL80211_ATTR_IFINDEX, ifi);

	struct nlgen* msg = send_recv(fd);

	nl_dump_gen(msg);

	return 0;
}
