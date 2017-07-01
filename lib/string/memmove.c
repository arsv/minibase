#include <string.h>

void* memmove(void* dst, const void* src, unsigned long n)
{
	if(dst < src)
		return memcpy(dst, src, n);
	else if(dst == src)
		return dst;

	void* r = dst;
	char* d = dst + n - 1;
	const char* s = src + n - 1;

	while(n--) *(d--) = *(s--);

	return r;
}
