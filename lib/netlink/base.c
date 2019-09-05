#include <cdefs.h>
#include "base.h"

int nl_len(struct nlmsg* msg)
{
	return msg->len;
}

struct nlmsg* nl_msg(void* buf, int len)
{
	struct nlmsg* msg = buf;

	if(len < sizeof(*msg))
		return NULL;
	if(len < msg->len)
		return NULL;

	return msg;
}
