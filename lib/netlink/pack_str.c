#include "base.h"
#include "ctx.h"
#include "pack.h"

#include <string.h>

void nl_put_str(struct netlink* nl, uint16_t type, const char* str)
{
	nl_put(nl, type, str, strlen(str) + 1);
}
