#include <cdefs.h>
#include <nlusctl.h>

static struct ucattr* attr_in(void* ptr, void* end)
{
	struct ucattr* at = ptr;

	void* hdrend = ptr + sizeof(*at);

	if(hdrend < ptr || hdrend > end)
		return NULL;

	void* payend = ptr + at->len;

	if(payend < ptr || payend > end)
		return NULL;

	return at;
}

struct ucattr* uc_get_0(struct ucattr* msg)
{
	void* buf = msg;
	void* ptr = buf + sizeof(*msg);
	void* end = buf + msg->len;

	return attr_in(ptr, end);
}

struct ucattr* uc_get_n(struct ucattr* msg, struct ucattr* at)
{
	void* buf = msg;
	void* ptr = at;
	void* end = buf + msg->len;
	int len = at->len;

	if(len <= 0) return NULL;

	len = (len + 3) & ~3;

	ptr += len;

	if(ptr < buf) return NULL;

	return attr_in(ptr, end);
}

struct ucattr* uc_get(struct ucattr* msg, int key)
{
	struct ucattr* at;

	for(at = uc_get_0(msg); at; at = uc_get_n(msg, at))
		if(at->key == key)
			return at;

	return NULL;
}

int uc_is_keyed(struct ucattr* at, int key)
{
	return (at->key == key);
}

void* uc_payload(struct ucattr* at)
{
	return (void*)(at->payload);
}

int uc_paylen(struct ucattr* at)
{
	return at->len - sizeof(*at);
}
