#include <netlink.h>
#include <netlink/pack.h>
#include <string.h>

void* nc_fixed(struct ncbuf* nc, uint size)
{
	void* buf = nc->brk;
	void* ptr = nc->ptr;
	void* end = nc->end;

	uint hdrlen = sizeof(struct nlmsg);

	if(size <= hdrlen) /* requested header shorter than nlmsg */
		goto over;
	if(ptr != buf + hdrlen) /* not called after nc_request */
		goto over;

	ptr = buf + size;

	if(ptr > end || ptr < buf) /* not enough space for header */
		goto over;

	nc->ptr = ptr;

	memzero(buf + hdrlen, size - hdrlen); /* clear non-nlmsg part */

	return buf;
over:
	nc->ptr = end + 1;

	return NULL;
}
