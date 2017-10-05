#include <sys/file.h>
#include <sys/mman.h>

#include <errtag.h>
#include <netlink.h>
#include <netlink/rtnl/mgrp.h>
#include <netlink/dump.h>
#include <util.h>

ERRTAG("rtevents");

char TX[1*1024];
char RX[7*1024];

int main(void)
{
	struct netlink nl;
	struct nlmsg* msg;

	nl_init(&nl);
	nl_set_txbuf(&nl, TX, sizeof(TX));
	nl_set_rxbuf(&nl, RX, sizeof(RX));
	
	int mgrp_link = RTMGRP_LINK | RTMGRP_NOTIFY;
	int mgrp_ipv4 = RTMGRP_IPV4_IFADDR | RTMGRP_IPV4_ROUTE;

	xchk(nl_connect(&nl, NETLINK_ROUTE, mgrp_link | mgrp_ipv4),
		"connect", "NETLINK_ROUTE");

	while((msg = nl_recv(&nl))) {
		nl_dump_rtnl(msg);
	} if(nl.err) {
		fail("nl-recv", NULL, nl.err);
	}

	return 0;
}
