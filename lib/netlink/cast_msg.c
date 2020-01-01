#include <netlink.h>
#include <cdefs.h>

struct nlmsg* nl_msg(void* buf, int len)
{
	struct nlmsg* msg = buf;

	if(len < 0)
		return NULL;

	unsigned ulen = (unsigned)len;

	if(ulen < sizeof(*msg))
		return NULL;
	if(ulen < msg->len)
		return NULL;

	return msg;
}
