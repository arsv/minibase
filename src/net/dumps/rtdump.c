#include <sys/fstat.h>
#include <sys/open.h>
#include <sys/mmap.h>

#include <netlink.h>
#include <netlink/dump.h>
#include <fail.h>

#include "common.h"

ERRTAG = "rtdump";

int main(int argc, char** argv)
{
	if(argc != 2)
		fail("bad call", NULL, 0);

	long len;
	char* buf = mmapwhole(argv[1], &len);

	struct nlmsg* msg = (struct nlmsg*) buf;

	if(msg->len > len)
		fail("incomplete message in", argv[1], 0);
	if(msg->len < len)
		warn("trailing garbage in", argv[1], 0);

	nl_dump_rtnl(msg);

	return 0;
}
