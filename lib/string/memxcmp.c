#include <bits/types.h>
#include <string.h>

/* Constant-time memcmp, for crypto stuff */

int memxcmp(const void* av, const void* bv, size_t len)
{
	const uint8_t* a = (const uint8_t*) av;
	const uint8_t* b = (const uint8_t*) bv;
	int ret = 0;

	while(len-- > 0)
		ret |= (*a++ - *b++);

	return ret;
}

