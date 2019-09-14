#include <netlink.h>
#include <netlink/pack.h>
#include <string.h>

void* nc_struct(struct ncbuf* nc, void* buf, uint size, uint hdrsize)
{
	if(hdrsize <= sizeof(struct nlmsg))
		goto over;
	if(hdrsize > size)
		goto over;

	nc->brk = buf;
	nc->ptr = buf + hdrsize;
	nc->end = buf + size;

	memzero(buf, hdrsize);

	return buf;
over:
	nc->ptr = nc->end + 1;

	return NULL;
}
