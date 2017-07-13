#include <string.h>
#include "../nlusctl.h"

static void* uc_alloc(struct ucbuf* uc, int len)
{
	int pad = (4 - len % 4) % 4;
	void* ret = uc->ptr;
	void* ptr = ret + len + pad;
	void* end = uc->end;

	if(ptr > end)
		uc->over = 1;
	if(uc->over)
		return NULL;

	/* stop valgrind from complaining about uninitialized padding */
	memzero(ret + len, pad);

	uc->ptr = ptr;

	return ret;
}

void uc_buf_set(struct ucbuf* uc, char* buf, int len)
{
	uc->brk = buf;
	uc->ptr = buf;
	uc->end = buf + len;
	uc->over = 0;
}

static struct ucmsg* uc_msg_hdr(struct ucbuf* uc)
{
	struct ucmsg* msg = (struct ucmsg*) uc->brk;

	if(uc->brk + sizeof(*msg) > uc->ptr)
		return NULL;

	return msg;
}

void uc_put_hdr(struct ucbuf* uc, int cmd)
{
	struct ucmsg* msg;

	uc->ptr = uc->brk;

	if(!(msg = uc_alloc(uc, sizeof(*msg))))
		return;

	msg->cmd = cmd;
	msg->len = 0; /* incomplete */
}

void uc_put_end(struct ucbuf* uc)
{
	struct ucmsg* msg;

	if(!(msg = uc_msg_hdr(uc)))
		return;

	msg->len = uc->ptr - uc->brk;
}

struct ucattr* uc_put_attr(struct ucbuf* uc, int key, int len)
{
	struct ucattr* at;
	int total = sizeof(*at) + len;

	if(!(at = uc_alloc(uc, total)))
		return NULL;

	at->len = total;
	at->key = key;

	return at;
}

void uc_put_bin(struct ucbuf* uc, int key, void* buf, int len)
{
	struct ucattr* at;

	if(!(at = uc_put_attr(uc, key, len)))
		return;

	memcpy(at->payload, buf, len);
}

void uc_put_int(struct ucbuf* uc, int key, int v)
{
	struct ucattr* at;

	if(!(at = uc_put_attr(uc, key, sizeof(v))))
		return;

	*((int*) at->payload) = v;
}

void uc_put_str(struct ucbuf* uc, int key, char* str)
{
	return uc_put_bin(uc, key, str, strlen(str) + 1);
}

void uc_put_flag(struct ucbuf* uc, int key)
{
	uc_put_attr(uc, key, 0);
}

struct ucattr* uc_put_nest(struct ucbuf* uc, int key)
{
	return uc_put_attr(uc, key, 0);
}

void uc_end_nest(struct ucbuf* uc, struct ucattr* at)
{
	if(!at) return; /* uc_put_nest overflowed uc */
	at->len = uc->ptr - (char*)at;
}
