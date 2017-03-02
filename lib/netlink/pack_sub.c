#include "base.h"
#include "ctx.h"
#include "pack.h"

struct nlattr* nl_put_nest(struct netlink* nl, uint16_t type)
{
	struct nlattr* at = nl_alloc(nl, sizeof(*at));

	if(!at) return at;

	at->type = type;
	at->len = sizeof(*at);

	return at;
}

void nl_end_nest(struct netlink* nl, struct nlattr* at)
{
	char* buf = nl->txbuf;
	char* end = buf + nl->txend;
	char* atp = (char*)(at);

	if(atp < buf || atp >= end - sizeof(*at))
		nl->txover = 1;
	else
		at->len = end - atp;
}
