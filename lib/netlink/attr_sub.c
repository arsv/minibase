#include <null.h>
#include "base.h"
#include "attr.h"

struct nlattr* nl_sub_0(struct nlattr* at)
{
	return nl_attr_0_in(at->payload, at->len - sizeof(*at));
}

struct nlattr* nl_sub_n(struct nlattr* at, struct nlattr* cur)
{
	return nl_attr_n_in(at->payload, at->len - sizeof(*at), cur);
}

struct nlattr* nl_sub(struct nlattr* at, uint16_t type)
{
	return nl_attr_k_in(at->payload, at->len - sizeof(*at), type);
}

void* nl_sub_of_len(struct nlattr* at, uint16_t type, int len)
{
	struct nlattr* ak = nl_sub(at, type);
	if(!ak) return NULL;
	return (ak->len == sizeof(*ak) + len) ? ak->payload : NULL;
}

char* nl_sub_str(struct nlattr* at, uint16_t type)
{
	return nl_str(nl_sub(at, type));
}
