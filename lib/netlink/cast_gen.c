#include <netlink.h>
#include <cdefs.h>

struct nlgen* nl_gen(struct nlmsg* msg)
{
	if(msg->len < sizeof(struct nlgen))
		return NULL;
	if(msg->type < 16)
		return NULL;

	return (struct nlgen*)msg;
}
