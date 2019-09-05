#include <string.h>
#include "base.h"
#include "cmd.h"

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
	nc->brk = NULL;
	nc->ptr = NULL;
	nc->end = NULL;

	return NULL;
}

void nc_header(struct nlmsg* msg, int type, int flags, int seq)
{
	msg->type = type;
	msg->flags = flags;
	msg->seq = seq;
	msg->pid = 0;
}

void nc_length(struct nlmsg* msg, struct ncbuf* nc)
{
	void* brk = nc->brk;
	void* ptr = nc->ptr;

	if(!brk) return;

	msg->len = ptr - brk;
}

static void* nc_put_(struct ncbuf* nc, uint key, uint len)
{
	void* ptr = nc->ptr;
	void* end = nc->end;
	uint full = sizeof(struct nlattr) + len;
	uint need = (full + 3) & ~3;

	if(end - ptr < need) {
		nc->brk = NULL;
		return NULL;
	}

	nc->ptr = ptr + need;

	struct nlattr* at = ptr;

	at->type = key;
	at->len = full;

	return at->payload;
}

void nc_put(struct ncbuf* nc, uint key, void* data, uint size)
{
	void* dst = nc_put_(nc, key, size);
	if(dst) memcpy(dst, data, size);
}

void nc_put_int(struct ncbuf* nc, uint key, int val)
{
	int* dst = nc_put_(nc, key, sizeof(*dst));
	if(dst) *dst = val;
}
