#include <cdefs.h>
#include <nlusctl.h>

static int extend_to_4bytes(int n)
{
	return n + ((4 - (n & 3)) & 3);
}

static int is_nest(char* buf, size_t len)
{
	char* end = buf + len;

	if(len <= sizeof(struct ucattr))
		return 0;

	char* p = buf;
	while(p < end) {
		struct ucattr* sub = (struct ucattr*) p;
		unsigned skip = extend_to_4bytes(sub->len);

		if(!skip) break;

		p += skip;
	}

	return (p == end);
}

struct ucattr* uc_is_nest(struct ucattr* at, int key)
{
	if(!at || at->key != key)
		return NULL;
	if(!is_nest(uc_payload(at), uc_paylen(at)))
		return NULL;

	return at;
}
