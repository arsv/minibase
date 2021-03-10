#include <string.h>

int strcmp(const char* sa, const char* sb)
{
	const byte* a = (const byte*)sa;
	const byte* b = (const byte*)sb;

	if(!a && !b) return 0;
	if(!a) return -*b;
	if(!b) return *a;

	while(*a && *a == *b) { a++; b++; }

	return (*a - *b);
}
