#include <netlink.h>
#include <netlink/pack.h>

#include <string.h>

void nc_header(struct ncbuf* nc, int type, int flags, int seq)
{
	void* buf = nc->brk;
	void* end = nc->end;
	struct nlmsg* msg = buf;

	void* ptr = buf + sizeof(*msg);

	if(ptr > end || ptr < buf) {
		ptr = end + 1;
		goto out;
	}

	msg->type = type;
	msg->flags = NLM_F_REQUEST | flags;
	msg->seq = seq;
	msg->pid = 0;
out:
	nc->ptr = ptr;
}
