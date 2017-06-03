#include <string.h>

/* memcpy() calls may be generated implicitly by gcc */

void* memcpy(void* dst, const void* src, unsigned long n)
{
	void* r = dst;
	char* d = dst;
	const char* s = src;

	while(n--) *(d++) = *(s++);

	return r;
}

/* This particular version of memcpy also works well as memmove */

void* memmove(void* dst, const void* src, unsigned long n)
	__attribute__((alias("memcpy")));
