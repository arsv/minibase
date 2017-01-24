#include <sys/fstat.h>
#include <sys/open.h>
#include <sys/mmap.h>

#include <netlink.h>
#include <netlink/dump.h>
#include <fail.h>

#include "common.h"

ERRTAG = "rtdump";

int align4(int n) { return n + (4 - n % 4) % 4; }

int main(int argc, char** argv)
{
	if(argc != 2)
		fail("bad call", NULL, 0);

	long len;
	char* buf = mmapwhole(argv[1], &len);
	long ptr = 0;

	while(ptr < len) {
		long left = len - ptr;

		if(left < sizeof(struct nlmsg))
			break;

		struct nlmsg* msg = (struct nlmsg*)(buf + ptr);

		if(msg->len > left)
			fail("incomplete message in", argv[1], 0);

		nl_dump_rtnl(msg);

		ptr += align4(msg->len);
	} if(ptr < len) {
		warn("trailing garbage in", argv[1], 0);
	}

	return 0;
}
