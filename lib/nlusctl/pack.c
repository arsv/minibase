#include <cdefs.h>
#include <string.h>
#include <nlusctl.h>

int uc_overflow(struct ucbuf* uc)
{
	void* buf = uc->buf;
	void* ptr = uc->ptr;
	void* end = uc->end;

	return (ptr > end || ptr < buf);
}

static void* uc_alloc(struct ucbuf* uc, int len)
{
	int pad = (4 - len % 4) % 4;
	void* buf = uc->buf;
	void* ret = uc->ptr;
	void* end = uc->end;
	void* ptr = ret + len + pad;

	if(ret > end || ret < buf) { /* already overflowed */
		return NULL;
	}
	if(ptr > end || ptr < buf) { /* overflow on this allocation */
		uc->end = ptr + 1;
		return NULL;
	}

	/* stop valgrind from complaining about uninitialized padding */
	memzero(ret + len, pad);

	uc->ptr = ptr;

	return ret;
}

void uc_buf_set(struct ucbuf* uc, char* buf, size_t len)
{
	uc->buf = buf;
	uc->ptr = buf;
	uc->end = buf + len;
}

void uc_put_hdr(struct ucbuf* uc, int cmd)
{
	struct ucmsg* msg;

	uc->ptr = uc->buf;

	if(!(msg = uc_alloc(uc, sizeof(*msg))))
		return;

	msg->cmd = cmd;
	msg->len = 0; /* incomplete */
}

void uc_put_end(struct ucbuf* uc)
{
	void* buf = uc->buf;
	void* ptr = uc->ptr;
	struct ucmsg* msg = buf;

	if(buf + sizeof(*msg) > ptr)
		return; /* invalid message */

	msg->len = uc->ptr - uc->buf;
}

void* uc_put_attr(struct ucbuf* uc, int key, size_t len)
{
	struct ucattr* at;
	int total = sizeof(*at) + len;

	if(!(at = uc_alloc(uc, total)))
		return NULL;

	at->len = total;
	at->key = key;

	return at->payload;
}

void uc_put_bin(struct ucbuf* uc, int key, void* buf, size_t len)
{
	void* ptr = uc_put_attr(uc, key, len);

	if(ptr) memcpy(ptr, buf, len);
}

void uc_put_int(struct ucbuf* uc, int key, int v)
{
	int* ptr = uc_put_attr(uc, key, sizeof(*ptr));

	if(ptr) *ptr = v;
}

void uc_put_i64(struct ucbuf* uc, int key, int64_t v)
{
	int64_t* ptr = uc_put_attr(uc, key, sizeof(*ptr));

	if(ptr) *ptr = v;
}

void uc_put_str(struct ucbuf* uc, int key, char* str)
{
	return uc_put_bin(uc, key, str, strlen(str) + 1);
}

void uc_put_flag(struct ucbuf* uc, int key)
{
	(void)uc_put_attr(uc, key, 0);
}

struct ucattr* uc_put_nest(struct ucbuf* uc, int key)
{
	struct ucattr* at;
	int total = sizeof(*at);

	if(!(at = uc_alloc(uc, total)))
		return NULL;

	at->len = total;
	at->key = key;

	return at;
}

void uc_end_nest(struct ucbuf* uc, struct ucattr* at)
{
	if(!at) return; /* uc_put_nest overflowed uc */
	at->len = uc->ptr - (char*)at;
}
