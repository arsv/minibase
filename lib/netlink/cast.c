#include <netlink.h>
#include <cdefs.h>

void* nl_cast(struct nlmsg* msg, int size)
{
	if(msg->len < size)
		return NULL;

	return msg;
}
