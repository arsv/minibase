#include <netlink.h>
#include <cdefs.h>

struct nlerr* nl_err(struct nlmsg* msg)
{
	if(msg->type != NLMSG_ERROR)
		return NULL;
	if(msg->len < sizeof(struct nlerr))
		return NULL;

	return (struct nlerr*)msg;
}
