#include "memcpy.h"

/* memcpy() calls may be generated implicitly by gcc */

void* memcpy(void* dst, const void* src, unsigned long n)
{
	void* r = dst;
	char* d = dst;
	const char* s = src;

	while(n--) *(d++) = *(s++);

	return r;
}
