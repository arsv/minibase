#include <null.h>
#include "base.h"
#include "attr.h"

int nl_check_zstr(char* buf, int len)
{
	int i;

	for(i = 0; i < len; i++)
		if(!buf[i])
			break;
	if(i >= len)
		return 0; /* not 0-terminated */
	if(i < len - 1)
		return 0; /* trailing garbage */

	return 1;
}

int nl_attr_is_zstr(struct nlattr* at)
{
	return nl_check_zstr(ATPAYLOAD(at));
}

char* nl_str(struct nlattr* at)
{
	return nl_check_zstr(ATPAYLOAD(at)) ? at->payload : NULL;
}
