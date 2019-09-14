#include <netlink.h>
#include <netlink/attr.h>
#include <cdefs.h>

struct nlattr* nl_sub_0(struct nlattr* at)
{
	return nl_attr_0_in(ATPAYLOAD(at));
}

struct nlattr* nl_sub_n(struct nlattr* at, struct nlattr* cur)
{
	return nl_attr_n_in(ATPAYLOAD(at), cur);
}

struct nlattr* nl_sub(struct nlattr* at, uint16_t type)
{
	return nl_attr_k_in(ATPAYLOAD(at), type);
}

void* nl_sub_of_len(struct nlattr* at, uint16_t type, size_t len)
{
	struct nlattr* ak = nl_sub(at, type);
	if(!ak) return NULL;
	return (ak->len == sizeof(*ak) + len) ? ak->payload : NULL;
}

char* nl_sub_str(struct nlattr* at, uint16_t type)
{
	return nl_str(nl_sub(at, type));
}
