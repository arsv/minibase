#include <string.h>

char* strchr(const char* str, int c)
{
	const char* p = str;

	for(p = str; *p; p++)
		if(*p == c)
			return (char*)p;

	return NULL;
}
