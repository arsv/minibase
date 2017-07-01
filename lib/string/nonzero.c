#include <string.h>

int nonzero(void* a, unsigned long n)
{
	char* p = a;
	char* e = a + n;

	for(; p < e; p++)
		if(*p) return 1;

	return 0;
}
