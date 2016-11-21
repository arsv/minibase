#include <string.h>

unsigned long strlen(const char* a)
{
	int l = 0;

	while(*a++) l++;

	return l;
}
