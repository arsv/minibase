#include <netlink.h>
#include <netlink/pack.h>
#include <cdefs.h>

struct nlmsg* nc_msg(struct ncbuf* nc)
{
	void* buf = nc->brk;
	void* ptr = nc->ptr;
	void* end = nc->end;

	if(ptr > end || ptr < buf)
		return NULL;

	long len = ptr - buf;
	struct nlmsg* msg = buf;
	void* mhe = buf + sizeof(*msg); /* message header end */

	if(mhe > ptr || mhe < buf)
		return NULL;

	msg->len = ptr - buf;

	return nl_msg(buf, len);
}
