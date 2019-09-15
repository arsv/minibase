#include <bits/errno.h>
#include <sys/socket.h>
#include <sys/creds.h>

#include <netlink.h>
#include <netlink/attr.h>
#include <netlink/pack.h>
#include <netlink/recv.h>
#include <netlink/dump.h>
#include <netlink/genl/ctrl.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>
#include <main.h>

/* The tool queries available GENL families and their multicast groups. */

ERRTAG("genlfams");

char rxbuf[4096];
char outbuf[4096];

static void dump_group(struct bufout* bo, struct nlattr* mg)
{
	char buf[200];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	uint32_t* id = nl_sub_u32(mg, 2);
	char* name = nl_sub_str(mg, 1);

	if(!id || !name)
		return;

	p = fmtstr(p, e, "    group ");
	p = fmtint(p, e, *id);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, name);
	*p++ = '\n';

	bufout(bo, buf, p - buf);
}

static void dump_groups(struct bufout* bo, struct nlgen* msg)
{
	struct nlattr* mg;
	struct nlattr* at;

	if(!(mg = nl_get_nest(msg, CTRL_ATTR_MCAST_GROUPS)))
		return;

	for(at = nl_sub_0(mg); at; at = nl_sub_n(mg, at))
		dump_group(bo, at);
}


static void dump_family(struct bufout* bo, struct nlgen* msg)
{
	char buf[100];
	char* p = buf;
	char* e = buf + sizeof(buf) - 1;

	uint16_t* id = nl_get_u16(msg, CTRL_ATTR_FAMILY_ID);
	char* name = nl_get_str(msg, CTRL_ATTR_FAMILY_NAME);

	if(!id || !name)
		return;

	p = fmtstr(p, e, "Family ");
	p = fmtint(p, e, *id);
	p = fmtstr(p, e, ": ");
	p = fmtstr(p, e, name);
	*p++ = '\n';

	bufout(bo, buf, p - buf);

	dump_groups(bo, msg);
}

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

int main(noargs)
{
	struct ncbuf nc;
	struct nrbuf nr;
	struct nlmsg* msg;
	struct nlerr* err;
	struct nlgen* gen;
	struct bufout bo;
	byte txbuf[64];
	int ret, fd;

	nc_buf_set(&nc, txbuf, sizeof(txbuf));
	nr_buf_set(&nr, rxbuf, sizeof(rxbuf));
	bufoutset(&bo, STDOUT, outbuf, sizeof(outbuf));

	fd = open_socket();

	nc_header(&nc, GENL_ID_CTRL, NLM_F_DUMP, 0);
	nc_gencmd(&nc, CTRL_CMD_GETFAMILY, 1);

	if((ret = nc_send(fd, &nc)) < 0)
		fail("send", NULL, ret);
recv:
	if((ret = nr_recv(fd, &nr)) < 0)
		fail("recv", NULL, ret);
next:
	if(!(msg = nr_next(&nr)))
		goto recv;
	if(msg->type == NLMSG_DONE)
		goto out;
	if((err = nl_err(msg)))
		fail(NULL, NULL, err->errno);
	if(!(gen = nl_gen(msg)))
		fail(NULL, NULL, -EBADMSG);

	dump_family(&bo, gen);
	goto next;
out:
	bufoutflush(&bo);

	return 0;
}
