#include <null.h>
#include "base.h"
#include "attr.h"

void* nl_bin(struct nlattr* at, int len)
{
	return (at->len - sizeof(*at) == len) ? at->payload : NULL;
}
