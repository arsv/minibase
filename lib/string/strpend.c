#include <string.h>

char* strpend(char* str)
{
	char* p = str;
	char* e = p + 4096;

	if(!p) goto out;

	for(; p < e; p++)
		if(!*p) return p;
out:
	return NULL;
}
