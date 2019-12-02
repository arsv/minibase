#include <nlusctl.h>
#include <cdefs.h>

/* Append (len, key) attribute to uc, followed by alloc bytes of payload
   space. Note len is the indicated length of the attribute, and alloc
   is what actually gets allocated in the buffer; uc_put_str would have
   alloc > len for padding, while uc_put_tail would have alloc = 0 with
   large non-zero len, because the data would come from a different iov.

   To maintain proper format, alloc must always be 4-bytes aligned. */

struct ucattr* uc_put(struct ucbuf* uc, int key, int len, int alloc)
{
	void* buf = uc->buf;
	int cap = uc->len;
	struct ucattr* msg = buf;
	struct ucattr* at;

	int full = sizeof(*at) + alloc;
	int ptr = msg->len;
	int new = ptr + full;

	if(!ptr)
		goto skip;
	if(full > cap)
		goto over;
	if(new <= 0)
		goto over;
	if(new > 0xFFFF)
		goto over;
	if(new > cap)
		goto over;

	msg->len = new;

	at = buf + ptr;
	at->key = key;
	at->len = len + sizeof(*at);

	return at;
over:
	msg->len = 0;
skip:
	return NULL;
}
