#include <string.h>

void memzero(void* a, unsigned long n)
{
	char* p = (char*) a;
	char* e = p + n;

	while(p < e) *p++ = 0;
}
