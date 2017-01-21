#include <null.h>
#include "base.h"
#include "attr.h"

static int extend_to_4bytes(int n)
{
	return n + ((4 - (n & 3)) & 3);
}

int nl_check_nest(char* buf, int len)
{
	char* end = buf + len;

	if(len <= sizeof(struct nlattr))
		return 0;

	char* p = buf;
	while(p < end) {
		struct nlattr* sub = (struct nlattr*) p;
		uint16_t skip = extend_to_4bytes(sub->len);

		if(!skip) break;

		p += skip;
	}

	return (p == end);
}

int nl_attr_is_nest(struct nlattr* at)
{
	return nl_check_nest(at->payload, nl_attr_len(at));
}

struct nlattr* nl_nest(struct nlattr* at)
{
	if(!nl_check_nest(at->payload, nl_attr_len(at)))
		return NULL;
	return at;
}
