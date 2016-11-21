#include <string.h>

void* memset(void* a, int c, unsigned long n)
{
	char* p = (char*) a;
	char* e = p + n;

	while(p < e) *p++ = c;

	return a;
}
