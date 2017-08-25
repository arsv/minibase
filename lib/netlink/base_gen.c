#include <bits/null.h>

#include "base.h"
#include "attr.h"

struct nlgen* nl_gen(struct nlmsg* msg)
{
	if(msg->len < sizeof(struct nlgen))
		return NULL;

	struct nlgen* gen = (struct nlgen*)msg;

	if(!nl_check_nest(gen->payload, msg->len - sizeof(*gen)))
		return NULL;

	return gen;
}
