#include <string.h>

int strncmp(const char* a, const char* b, size_t n)
{
	if(!n) return 0;
	while(*a && --n > 0 && *a == *b) { a++; b++; }
	return (*a - *b);

}
