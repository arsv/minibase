#include <netlink.h>
#include <netlink/pack.h>
#include <cdefs.h>

struct nlattr* nc_put_nest(struct ncbuf* nc, uint16_t type)
{
	uint len = sizeof(struct nlattr);
	void* buf = nc->brk;
	void* ret = nc->ptr;
	void* end = nc->end;
	void* ptr = ret + len;

	if(ret > end || ret < buf) {
		return NULL;
	} if(ptr > end || ptr < buf) {
		nc->ptr = end + 1;
		return NULL;
	} if(ptr < buf + sizeof(struct nlmsg)) {
		return NULL;
	}

	nc->ptr = ptr;

	struct nlattr* at = ret;

	at->type = type;

	return at;
}

void nc_end_nest(struct ncbuf* nc, struct nlattr* at)
{
	void* atp = (void*)at;
	void* buf = nc->brk;
	void* ptr = nc->ptr;

	if(atp < buf || atp > ptr)
		return;
	if(atp + sizeof(*at) > ptr)
		return;

	at->len = ptr - atp;
}
