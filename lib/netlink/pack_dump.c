#include <netlink.h>
#include <netlink/pack.h>
#include <cdefs.h>

struct nlmsg* nc_message(struct ncbuf* nc)
{
	void* buf = nc->brk;
	void* ptr = nc->ptr;
	void* end = nc->end;

	if(ptr > end || ptr < buf)
		return NULL;

	long len = ptr - buf;

	return nl_msg(buf, len);
}
