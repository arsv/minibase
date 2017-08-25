#include <sys/file.h>

#include <errtag.h>
#include <netlink.h>
#include <netlink/dump.h>
#include <util.h>

#include "common.h"

ERRTAG("rtdump");

int align4(int n) { return n + (4 - n % 4) % 4; }

int main(int argc, char** argv)
{
	long len;
	char* buf;

	if(argc == 2)
		buf = mmapwhole(argv[1], &len);
	else if(argc == 1)
		buf = readwhole(STDIN, &len);
	else
		fail("bad call", NULL, 0);

	long ptr = 0;

	while(ptr < len) {
		long left = len - ptr;

		if(left < sizeof(struct nlmsg))
			break;

		struct nlmsg* msg = (struct nlmsg*)(buf + ptr);

		if(msg->len > left)
			fail("incomplete message", NULL, 0);

		nl_dump_rtnl(msg);

		ptr += align4(msg->len);
	} if(ptr < len) {
		warn("trailing garbage", NULL, 0);
	}

	return 0;
}
