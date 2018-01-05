#include <bits/errno.h>

#include <errtag.h>
#include <netlink.h>
#include <netlink/genl/ctrl.h>
#include <netlink/dump.h>
#include <string.h>
#include <format.h>
#include <output.h>
#include <util.h>

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

int main(void)
{
	struct netlink nl;
	struct nlgen* msg;
	char txbuf[100];
	int ret;

	nl_init(&nl);
	nl_set_txbuf(&nl, txbuf, sizeof(txbuf));
	nl_set_rxbuf(&nl, rxbuf, sizeof(rxbuf));
	nl_connect(&nl, NETLINK_GENERIC, 0);

	nl_new_cmd(&nl, GENL_ID_CTRL, CTRL_CMD_GETFAMILY, 1);

	if((ret = nl_send_dump(&nl)) < 0)
		fail("send", NULL, ret);

	struct bufout bo = {
		.fd = STDOUT,
		.buf = outbuf,
		.ptr = 0,
		.len = sizeof(outbuf)
	};

	while((msg = nl_recv_genl_multi(&nl)))
		dump_family(&bo, msg);

	bufoutflush(&bo);

	return 0;
}
