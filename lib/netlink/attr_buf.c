#include <cdefs.h>

#include "base.h"
#include "attr.h"

static size_t extend_to_4bytes(size_t n)
{
	return n + ((4 - (n & 3)) & 3);
}

struct nlattr* nl_attr_0_in(char* buf, size_t len)
{
	struct nlattr* at;

	if(len < sizeof(*at))
		return NULL;

	at = (struct nlattr*)buf;

	if(at->len > len)
		return NULL;

	return at;
}

static int ptr_in_buf(char* buf, size_t len, char* ptr)
{
	if(!ptr)
		return 0;
	if(ptr < buf)
		return 0;
	else if(ptr >= buf + len)
		return 0;
	return 1;
}

struct nlattr* nl_attr_n_in(char* buf, size_t len, struct nlattr* curr)
{
	char* pcurr = (char*)curr;

	if(!ptr_in_buf(buf, len, pcurr)) return NULL;

	if(curr->len < sizeof(*curr)) return NULL;

	char* pnext = pcurr + extend_to_4bytes(curr->len);

	if(!ptr_in_buf(buf, len, pnext)) return NULL;

	return (struct nlattr*)pnext;
}

struct nlattr* nl_attr_k_in(char* buf, size_t len, int type)
{
	struct nlattr* at;

	for(at = nl_attr_0_in(buf, len); at; at = nl_attr_n_in(buf, len, at))
		if(at->type == type)
			return at;

	return NULL;
}
