#include "strapp.h"

char* strapp(char* dst, char* end, const char* src)
{
	char* p = dst;
	const char* q = src;

	while(p < end && *q)
		*p++ = *q++;

	return p;
}
