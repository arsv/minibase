#include <bits/types.h>
#include <string.h>

int memcmp(const void* av, const void* bv, size_t len)
{
	const uint8_t* a = (const uint8_t*) av;
	const uint8_t* b = (const uint8_t*) bv;
	int d;

	while(len-- > 0)
		if((d = (*a++ - *b++)))
			return d;

	return 0;
}

