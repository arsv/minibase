#include <string.h>

int memcmp(const void* av, const void* bv, long len)
{
	const char* a = (const char*) av;
	const char* b = (const char*) bv;

	while(len-- > 0)
		if(*a++ != *b++)
			return 1;
	return 0;
}

