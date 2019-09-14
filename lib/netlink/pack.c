#include <netlink.h>
#include <netlink/pack.h>

void nc_buf_set(struct ncbuf* nc, void* buf, uint size)
{
	nc->brk = buf;
	nc->ptr = buf;
	nc->end = buf + size;
}
